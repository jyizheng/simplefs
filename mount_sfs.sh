#!/bin/sh


rm image

dd bs=4096 count=100 if=/dev/zero of=image
./mkfs-sfs image

sudo umount tmp
sudo rmmod simplefs
sudo insmod simplefs.ko
sudo mount -o loop -t sfs image `pwd`/tmp

