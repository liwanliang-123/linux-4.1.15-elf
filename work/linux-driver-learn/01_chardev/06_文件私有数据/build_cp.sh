#!/bin/bash

make

./user_build.sh user.c

#scp -oHostKeyAlgorithms=+ssh-rsa ./*.ko root@192.168.1.79:~
scp -oHostKeyAlgorithms=+ssh-rsa ./*.ko root@192.168.1.162:~

#scp -oHostKeyAlgorithms=+ssh-rsa out root@192.168.1.79:~
scp -oHostKeyAlgorithms=+ssh-rsa out root@192.168.1.162:~

make clean
rm out
