#!/bin/bash

rm atomic
./user_build.sh atomic_user.c atomic

make
scp -oHostKeyAlgorithms=+ssh-rsa ./*.ko root@192.168.1.55:~
scp -oHostKeyAlgorithms=+ssh-rsa atomic root@192.168.1.55:~
make clean
