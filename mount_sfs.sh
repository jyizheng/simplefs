#!/bin/sh

make clean
make

sudo ./mkfs-sfs /dev/sda1


sudo umount tmp
sudo rmmod simplefs
sudo insmod simplefs.ko
#sudo mount -o loop -t sfs /dev/sda1 `pwd`/tmp
sudo mount -t sfs /dev/sda1 `pwd`/tmp
