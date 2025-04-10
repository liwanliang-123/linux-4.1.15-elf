#!/bin/sh

#编译所以设备树文件
#make ARCH=arm CROSS_COMPILE=arm-poky-linux-gnueabi-  dtbs

#编译指定设备树文件
#make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- imx6ull-elf1-emmc.dtb

set -e

make ARCH=arm CROSS_COMPILE=arm-poky-linux-gnueabi- imx6ull-elf1-emmc.dtb
#scp -oHostKeyAlgorithms=+ssh-rsa ./arch/arm/boot/dts/imx6ull-elf1-emmc.dtb root@192.168.1.161:/run/media/mmcblk1p1
scp -oHostKeyAlgorithms=+ssh-rsa ./arch/arm/boot/dts/imx6ull-elf1-emmc.dtb root@192.168.1.55:/run/media/mmcblk1p1
#scp ./arch/arm/boot/dts/imx6ull-elf1-emmc.dtb root@192.168.1.55:/run/media/mmcblk1p1
