#!/bin/sh

mkdir /sys/kernel/config/usb_gadget/g1  -m 0770 
sleep 1
echo 0x8888 > /sys/kernel/config/usb_gadget/g1/idVendor 
echo 0x6666 > /sys/kernel/config/usb_gadget/g1/idProduct


mkdir /sys/kernel/config/usb_gadget/g1/strings/0x409   -m 0770 
sleep 1
echo "0123456789ABCDEF" > /sys/kernel/config/usb_gadget/g1/strings/0x409/serialnumber
echo "google"  > /sys/kernel/config/usb_gadget/g1/strings/0x409/manufacturer
echo "android"  > /sys/kernel/config/usb_gadget/g1/strings/0x409/product


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

UDC_NAME=`ls /sys/class/udc/| awk '{print $1}'`
echo $UDC_NAME > /sys/kernel/config/usb_gadget/g1/UDC
