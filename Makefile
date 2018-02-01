obj-m := simplefs.o
simplefs-objs := sfs.o
ccflag-y := -DSFS_DEBUG

all: ko mkfs-sfs

ko:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

mkfs-sfs_SOURCES:
	mkfs-sfs.c sfs.h

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm mkfs-sfs
