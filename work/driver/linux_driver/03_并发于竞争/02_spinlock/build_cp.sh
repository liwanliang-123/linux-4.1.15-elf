#!/bin/bash

rm spinlock
./user_build.sh spinlock_user.c spinlock

make
scp -oHostKeyAlgorithms=+ssh-rsa ./*.ko root@192.168.1.55:~
scp -oHostKeyAlgorithms=+ssh-rsa spinlock root@192.168.1.55:~
make clean
