#!/bin/sh

insmod /lib/modules/4.19.91/kernel/drivers/usb/gadget/udc/fotg211-udc.ko
mount -t configfs none /sys/kernel/config



mkdir /sys/kernel/config/usb_gadget/g1  -m 0770 
sleep 1
echo 0x8888 > /sys/kernel/config/usb_gadget/g1/idVendor 
echo 0x6666 > /sys/kernel/config/usb_gadget/g1/idProduct


mkdir /sys/kernel/config/usb_gadget/g1/strings/0x409   -m 0770 
sleep 1
echo "0123456789ABCDEF" > /sys/kernel/config/usb_gadget/g1/strings/0x409/serialnumber
echo "berxel"  > /sys/kernel/config/usb_gadget/g1/strings/0x409/manufacturer
echo "usbcam"  > /sys/kernel/config/usb_gadget/g1/strings/0x409/product


##UVC
mkdir /sys/kernel/config/usb_gadget/g1/configs/b.2  -m 0770 
mkdir /sys/kernel/config/usb_gadget/g1/configs/b.2/strings/0x409  -m 0770 
echo "uvc" > /sys/kernel/config/usb_gadget/g1/configs/b.2/strings/0x409/configuration

mkdir /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0
mkdir -p /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/streaming/mjpeg/m/720p
echo 1280 > /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/streaming/mjpeg/m/720p/wWidth
echo 720 > /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/streaming/mjpeg/m/720p/wHeight
echo 333333 > /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/streaming/mjpeg/m/720p/dwFrameInterval
echo 333333 > /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/streaming/mjpeg/m/720p/dwDefaultFrameInterval
echo 442368000 > /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/streaming/mjpeg/m/720p/dwMinBitRate
echo 442368000 > /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/streaming/mjpeg/m/720p/dwMaxBitRate
echo 1843200 > /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/streaming/mjpeg/m/720p/dwMaxVideoFrameBufferSize
mkdir /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/streaming/header/h
ln -s /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/streaming/mjpeg/m/ /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/streaming/header/h/
ln -s /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/streaming/header/h/ /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/streaming/class/fs
ln -s /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/streaming/header/h/ /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/streaming/class/hs
mkdir /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/control/header/h
ln -s /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/control/header/h/ /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/control/class/fs/
ln -s /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/control/header/h/ /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/control/class/hs/
ln -s /sys/kernel/config/usb_gadget/g1/functions/uvc.usb0/ /sys/kernel/config/usb_gadget/g1/configs/b.2/uvc.usb0


##adb
mkdir /sys/kernel/config/usb_gadget/g1/configs/b.1  -m 0770 
mkdir /sys/kernel/config/usb_gadget/g1/configs/b.1/strings/0x409  -m 0770 
echo "adb" > /sys/kernel/config/usb_gadget/g1/configs/b.1/strings/0x409/configuration
sleep 1
mkdir /sys/kernel/config/usb_gadget/g1/functions/ffs.adb
ln -s  /sys/kernel/config/usb_gadget/g1/functions/ffs.adb /sys/kernel/config/usb_gadget/g1/configs/b.1
sleep 1
mkdir /dev/usb-ffs -m 0770 
mkdir /dev/usb-ffs/adb -m 0770 
mount -t functionfs adb /dev/usb-ffs/adb
adbd  &

sleep 1
##UDC_NAME=`ls /sys/class/udc/| awk '{print $1}'`
##echo $UDC_NAME > /sys/kernel/config/usb_gadget/g1/UDC

echo f0600000.nvt_usb2dev > /sys/kernel/config/usb_gadget/g1/UDC