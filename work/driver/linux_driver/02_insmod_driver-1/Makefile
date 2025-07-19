KDIR = ../../../src/linux-4.1.15

export ARCH=arm
export CROSS_COMPILE=arm-poky-linux-gnueabi-

# KBUILD_CFLAGS_MODULE  += -DDEBUG # 在 Makefile 文件中添加条件编译的宏条件
#KBUILD_CFLAGS_MODULE  += -DDEBUG1=5 # 将 DEBUG 值定义为 5
# KBUILD_CFLAGS_KERNEL  += -DDEBUG # 如果是将驱动编译进内核，则使用这个变量
#EXTRA_CFLAGS += -DDEBUG2 # 用这个变量也可以用来定义 条件编译宏

obj-m += insmod_driver.o

PWD?=$(shell pwd) #获取 Makefile 所在路径
all:
#进入到 KDIR 目录，使用 PWD 路径下源码和 Makefile 文件编译驱动模块
	make -C $(KDIR) M=$(PWD) modules
clean:
	make -C $(KDIR) M=`pwd` modules clean
	#rm -f *.ko *.o *.mod.c *.symvers *.order
