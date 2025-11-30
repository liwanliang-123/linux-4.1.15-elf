#!/bin/bash

#arm-poky-linux-gnueabi-gcc $1 --sysroot=/opt/fsl-imx-x11/4.1.15-2.0.0/sysroots/cortexa7hf-neon-poky-linux-gnueabi/ -mfloat-abi=hard -mfpu=neon -o $2

arm-poky-linux-gnueabi-g++ $1 --sysroot=/opt/fsl-imx-x11/4.1.15-2.0.0/sysroots/cortexa7hf-neon-poky-linux-gnueabi/ -mfloat-abi=hard -mfpu=neon $2 $3
