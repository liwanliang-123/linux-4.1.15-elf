#!/bin/bash

./user_build.sh insmod.c app

make
scp -oHostKeyAlgorithms=+ssh-rsa ./*.ko app root@192.168.1.55:~
make clean

rm -f app
