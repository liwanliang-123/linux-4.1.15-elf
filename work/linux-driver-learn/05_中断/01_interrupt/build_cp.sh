#!/bin/bash

make
scp -oHostKeyAlgorithms=+ssh-rsa ./*.ko root@192.168.1.161:~
make clean
