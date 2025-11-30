KDIR = ../../../src/linux-4.1.15

export ARCH=arm
export CROSS_COMPILE=arm-poky-linux-gnueabi-

obj-m += fasync_driver.o

PWD?=$(shell pwd)
all:
	make -C $(KDIR) M=$(PWD) modules
clean:
	make -C $(KDIR) M=`pwd` modules clean
