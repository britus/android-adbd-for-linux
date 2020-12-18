/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

#include "sysdeps.h"

#define  TRACE_TAG  TRACE_SOCKETS
#include "adb.h"

ADB_MUTEX_DEFINE( socket_list_lock );

static void local_socket_close_locked(asocket *s);

//extern int online;

static unsigned local_socket_next_id = 1;

static asocket local_socket_list = {
    .next = &local_socket_list,
    .prev = &local_socket_list,
};

/* the the list of currently closing local sockets.
** these have no peer anymore, but still packets to
** write to their fd.
*/
static asocket local_socket_closing_list = {
    .next = &local_socket_closing_list,
    .prev = &local_socket_closing_list,
};

asocket *find_local_socket(unsigned id)
{
    asocket *s;
    asocket *result = NULL;

    adb_mutex_lock(&socket_list_lock);
    for (s = local_socket_list.next; s != &local_socket_list; s = s->next) {
        if (s->id == id) {
            result = s;
            break;
        }
    }
    adb_mutex_unlock(&socket_list_lock);

    return result;
}

static void
insert_local_socket(asocket*  s, asocket*  list)
{
    s->next       = list;
    s->prev       = s->next->prev;
    s->prev->next = s;
    s->next->prev = s;
}


void install_local_socket(asocket *s)
{
    adb_mutex_lock(&socket_list_lock);

    s->id = local_socket_next_id++;
    insert_local_socket(s, &local_socket_list);

    adb_mutex_unlock(&socket_list_lock);
}

void remove_socket(asocket *s)
{
    // socket_list_lock should already be held
    if (s->prev && s->next)
    {
        s->prev->next = s->next;
        s->next->prev = s->prev;
        s->next = 0;
        s->prev = 0;
        s->id = 0;
    }
}

void close_all_sockets(atransport *t)
{
    asocket *s;

        /* this is a little gross, but since s->close() *will* modify
        ** the list out from under you, your options are limited.
        */
    adb_mutex_lock(&socket_list_lock);
restart:
    for(s = local_socket_list.next; s != &local_socket_list; s = s->next){
        if(s->transport == t || (s->peer && s->peer->transport == t)) {
            local_socket_close_locked(s);
            goto restart;
        }
    }
    adb_mutex_unlock(&socket_list_lock);
}

static int local_socket_enqueue(asocket *s, apacket *p)
{
    D("LS(%d): enqueue %d\n", s->id, p->len);

    p->ptr = p->data;

        /* if there is already data queue'd, we will receive
        ** events when it's time to write.  just add this to
        ** the tail
        */
    if(s->pkt_first) {
        goto enqueue;
    }

        /* write as much as we can, until we
        ** would block or there is an error/eof
        */
    while(p->len > 0) {
        int r = adb_write(s->fd, p->ptr, p->len);
        if(r > 0) {
            p->len -= r;
            p->ptr += r;
            continue;
        }
        if((r == 0) || (errno != EAGAIN)) {
            D( "LS(%d): not ready, errno=%d: %s\n", s->id, errno, strerror(errno) );
            s->close(s);
            return 1; /* not ready (error) */
        } else {
            break;
        }
    }

    if(p->len == 0) {
        put_apacket(p);
        return 0; /* ready for more data */
    }

enqueue:
    p->next = 0;
    if(s->pkt_first) {
        s->pkt_last->next = p;
    } else {
        s->pkt_first = p;
    }
    s->pkt_last = p;

        /* make sure we are notified when we can drain the queue */
    fdevent_add(&s->fde, FDE_WRITE);

    return 1; /* not ready (backlog) */
}

static void local_socket_ready(asocket *s)
{
        /* far side is ready for data, pay attention to
           readable events */
    fdevent_add(&s->fde, FDE_READ);
//    D("LS(%d): ready()\n", s->id);
}

static void local_socket_close(asocket *s)
{
    adb_mutex_lock(&socket_list_lock);
    local_socket_close_locked(s);
    adb_mutex_unlock(&socket_list_lock);
}

// be sure to hold the socket list lock when calling this
static void local_socket_destroy(asocket  *s)
{
    apacket *p, *n;
    int exit_on_close = s->exit_on_close;

    D("LS(%d): destroying fde.fd=%d\n", s->id, s->fde.fd);

        /* IMPORTANT: the remove closes the fd
        ** that belongs to this socket
        */
    fdevent_remove(&s->fde);

        /* dispose of any unwritten data */
    for(p = s->pkt_first; p; p = n) {
        D("LS(%d): discarding %d bytes\n", s->id, p->len);
        n = p->next;
        put_apacket(p);
    }
    remove_socket(s);
    free(s);

    if (exit_on_close) {
        D("local_socket_destroy: exiting\n");
        exit(1);
    }
}


static void local_socket_close_locked(asocket *s)
{
    D("entered. LS(%d) fd=%d\n", s->id, s->fd);
    if(s->peer) {
        D("LS(%d): closing peer. peer->id=%d peer->fd=%d\n",
          s->id, s->peer->id, s->peer->fd);
        s->peer->peer = 0;
        // tweak to avoid deadlock
        if (s->peer->close == local_socket_close) {
            local_socket_close_locked(s->peer);
        } else {
            s->peer->close(s->peer);
        }
        s->peer = 0;
    }

        /* If we are already closing, or if there are no
        ** pending packets, destroy immediately
        */
    if (s->closing || s->pkt_first == NULL) {
        int   id = s->id;
        local_socket_destroy(s);
        D("LS(%d): closed\n", id);
        return;
    }

        /* otherwise, put on the closing list
        */
    D("LS(%d): closing\n", s->id);
    s->closing = 1;
    fdevent_del(&s->fde, FDE_READ);
    remove_socket(s);
    D("LS(%d): put on socket_closing_list fd=%d\n", s->id, s->fd);
    insert_local_socket(s, &local_socket_closing_list);
}

static void local_socket_event_func(int fd, unsigned ev, void *_s)
{
    asocket *s = _s;

    D("LS(%d): event_func(fd=%d(==%d), ev=%04x)\n", s->id, s->fd, fd, ev);

    /* put the FDE_WRITE processing before the FDE_READ
    ** in order to simplify the code.
    */
    if(ev & FDE_WRITE){
        apacket *p;

        while((p = s->pkt_first) != 0) {
            while(p->len > 0) {
                int r = adb_write(fd, p->ptr, p->len);
                if(r > 0) {
                    p->ptr += r;
                    p->len -= r;
                    continue;
                }
                if(r < 0) {
                    /* returning here is ok because FDE_READ will
                    ** be processed in the next iteration loop
                    */
                    if(errno == EAGAIN) return;
                    if(errno == EINTR) continue;
                }
                D(" closing after write because r=%d and errno is %d\n", r, errno);
                s->close(s);
                return;
            }

            if(p->len == 0) {
                s->pkt_first = p->next;
                if(s->pkt_first == 0) s->pkt_last = 0;
                put_apacket(p);
            }
        }

            /* if we sent the last packet of a closing socket,
            ** we can now destroy it.
            */
        if (s->closing) {
            D(" closing because 'closing' is set after write\n");
            s->close(s);
            return;
        }

            /* no more packets queued, so we can ignore
            ** writable events again and tell our peer
            ** to resume writing
            */
        fdevent_del(&s->fde, FDE_WRITE);
        s->peer->ready(s->peer);
    }


    if(ev & FDE_READ){
        apacket *p = get_apacket();
        unsigned char *x = p->data;
        size_t avail = MAX_PAYLOAD;
        int r;
        int is_eof = 0;

        while(avail > 0) {
            r = adb_read(fd, x, avail);
            D("LS(%d): post adb_read(fd=%d,...) r=%d (errno=%d) avail=%d\n", s->id, s->fd, r, r<0?errno:0, avail);
            if(r > 0) {
                avail -= r;
                x += r;
                continue;
            }
            if(r < 0) {
                if(errno == EAGAIN) break;
                if(errno == EINTR) continue;
            }

                /* r = 0 or unhandled error */
            is_eof = 1;
            break;
        }
        D("LS(%d): fd=%d post avail loop. r=%d is_eof=%d forced_eof=%d\n",
          s->id, s->fd, r, is_eof, s->fde.force_eof);
        if((avail == MAX_PAYLOAD) || (s->peer == 0)) {
            put_apacket(p);
        } else {
            p->len = MAX_PAYLOAD - avail;

            r = s->peer->enqueue(s->peer, p);
            D("LS(%d): fd=%d post peer->enqueue(). r=%d\n", s->id, s->fd, r);

            if(r < 0) {
                    /* error return means they closed us as a side-effect
                    ** and we must return immediately.
                    **
                    ** note that if we still have buffered packets, the
                    ** socket will be placed on the closing socket list.
                    ** this handler function will be called again
                    ** to process FDE_WRITE events.
                    */
                return;
            }

            if(r > 0) {
                    /* if the remote cannot accept further events,
                    ** we disable notification of READs.  They'll
                    ** be enabled again when we get a call to ready()
                    */
                fdevent_del(&s->fde, FDE_READ);
            }
        }
        /* Don't allow a forced eof if data is still there */
        if((s->fde.force_eof && !r) || is_eof) {
            D(" closing because is_eof=%d r=%d s->fde.force_eof=%d\n", is_eof, r, s->fde.force_eof);
            s->close(s);
        }
    }

    if(ev & FDE_ERROR){
            /* this should be caught be the next read or write
            ** catching it here means we may skip the last few
            ** bytes of readable data.
            */
//        s->close(s);
        D("LS(%d): FDE_ERROR (fd=%d)\n", s->id, s->fd);

        return;
    }
}

asocket *create_local_socket(int fd)
{
    asocket *s = calloc(1, sizeof(asocket));
    if (s == NULL) fatal("cannot allocate socket");
    s->fd = fd;
    s->enqueue = local_socket_enqueue;
    s->ready = local_socket_ready;
    s->close = local_socket_close;
    install_local_socket(s);

    fdevent_install(&s->fde, fd, local_socket_event_func, s);
/*    fdevent_add(&s->fde, FDE_ERROR); */
    //fprintf(stderr, "Created local socket in create_local_socket \n");
    D("LS(%d): created (fd=%d)\n", s->id, s->fd);
    return s;
}

asocket *create_local_service_socket(const char *name)
{
    asocket *s;
    int fd;

    fd = service_to_fd(name);
    if(fd < 0) return 0;

    s = create_local_socket(fd);
    D("LS(%d): bound to '%s' via %d\n", s->id, name, fd);

    if ((!strncmp(name, "root:", 5) && getuid() != 0)
        || !strncmp(name, "usb:", 4)
        || !strncmp(name, "tcpip:", 6)) {
        D("LS(%d): enabling exit_on_close\n", s->id);
        s->exit_on_close = 1;
    }

    return s;
}

/* a Remote socket is used to send/receive data to/from a given transport object
** it needs to be closed when the transport is forcibly destroyed by the user
*/
typedef struct aremotesocket {
    asocket      socket;
    adisconnect  disconnect;
} aremotesocket;

static int remote_socket_enqueue(asocket *s, apacket *p)
{
    D("entered remote_socket_enqueue RS(%d) WRITE fd=%d peer.fd=%d\n",
      s->id, s->fd, s->peer->fd);
    p->msg.command = A_WRTE;
    p->msg.arg0 = s->peer->id;
    p->msg.arg1 = s->id;
    p->msg.data_length = p->len;
    send_packet(p, s->transport);
    return 1;
}

static void remote_socket_ready(asocket *s)
{
    D("entered remote_socket_ready RS(%d) OKAY fd=%d peer.fd=%d\n",
      s->id, s->fd, s->peer->fd);
    apacket *p = get_apacket();
    p->msg.command = A_OKAY;
    p->msg.arg0 = s->peer->id;
    p->msg.arg1 = s->id;
    send_packet(p, s->transport);
}

static void remote_socket_close(asocket *s)
{
    D("entered remote_socket_close RS(%d) CLOSE fd=%d peer->fd=%d\n",
      s->id, s->fd, s->peer?s->peer->fd:-1);
    apacket *p = get_apacket();
    p->msg.command = A_CLSE;
    if(s->peer) {
        p->msg.arg0 = s->peer->id;
        s->peer->peer = 0;
        D("RS(%d) peer->close()ing peer->id=%d peer->fd=%d\n",
          s->id, s->peer->id, s->peer->fd);
        s->peer->close(s->peer);
    }
    p->msg.arg1 = s->id;
    send_packet(p, s->transport);
    D("RS(%d): closed\n", s->id);
    remove_transport_disconnect( s->transport, &((aremotesocket*)s)->disconnect );
    free(s);
}

static void remote_socket_disconnect(void*  _s, atransport*  t)
{
    asocket*  s    = _s;
    asocket*  peer = s->peer;

    D("remote_socket_disconnect RS(%d)\n", s->id);
    if (peer) {
        peer->peer = NULL;
        peer->close(peer);
    }
    remove_transport_disconnect( s->transport, &((aremotesocket*)s)->disconnect );
    free(s);
}

asocket *create_remote_socket(unsigned id, atransport *t)
{
    asocket *s = calloc(1, sizeof(aremotesocket));
    adisconnect*  dis = &((aremotesocket*)s)->disconnect;

    if (s == NULL) fatal("cannot allocate socket");
    s->id = id;
    s->enqueue = remote_socket_enqueue;
    s->ready = remote_socket_ready;
    s->close = remote_socket_close;
    s->transport = t;

    dis->func   = remote_socket_disconnect;
    dis->opaque = s;
    add_transport_disconnect( t, dis );
    D("RS(%d): created\n", s->id);
    return s;
}
