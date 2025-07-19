#!/bin/bash

rm mutex
./user_build.sh mutex_user.c mutex

make
scp -oHostKeyAlgorithms=+ssh-rsa ./*.ko root@192.168.1.55:~
scp -oHostKeyAlgorithms=+ssh-rsa mutex root@192.168.1.55:~
make clean
