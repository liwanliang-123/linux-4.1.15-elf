#!/bin/bash

rm semaphore
./user_build.sh semaphore_user.c semaphore

make
scp -oHostKeyAlgorithms=+ssh-rsa ./*.ko root@192.168.1.55:~
scp -oHostKeyAlgorithms=+ssh-rsa semaphore root@192.168.1.55:~
make clean
