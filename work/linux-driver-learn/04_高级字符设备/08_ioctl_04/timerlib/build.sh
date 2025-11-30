#!/bin/bash

set -e

arm-poky-linux-gnueabi-gcc ./timer*.c  \
    -c --sysroot=/opt/fsl-imx-x11/4.1.15-2.0.0/sysroots/cortexa7hf-neon-poky-linux-gnueabi/ \
    -mfloat-abi=hard -mfpu=neon

arm-poky-linux-gnueabi-ar rcs ./libtimer.a timerclose.o timeropen.o timerset.o

arm-poky-linux-gnueabi-gcc ./user.c \
    --sysroot=/opt/fsl-imx-x11/4.1.15-2.0.0/sysroots/cortexa7hf-neon-poky-linux-gnueabi/ \
    -mfloat-abi=hard -mfpu=neon -L./ -ltimer

cp -v ./a.out ../
rm ./a.out ./*.o ./*.a
