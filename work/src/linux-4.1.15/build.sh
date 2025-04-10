#!/bin/bash
export CPUS=`grep -c processor /proc/cpuinfo`
source /opt/fsl-imx-x11/4.1.15-2.0.0/environment-setup-cortexa7hf-neon-poky-linux-gnueabi

make distclean
make imx6ull_elf1_defconfig
make zImage -j${CPUS}
make dtbs -j${CPUS}
make modules -j${CPUS}

rm -rf ./.tmp
make modules_install INSTALL_MOD_PATH=./.tmp/rootfs/
cd .tmp/rootfs/
tar -jcvf modules.tar.bz2 *
