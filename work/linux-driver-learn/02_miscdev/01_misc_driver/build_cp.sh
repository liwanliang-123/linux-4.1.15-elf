#!/bin/bash

make

./user_build.sh user.c

if [ $1 -lt 1 ]; then
	scp -oHostKeyAlgorithms=+ssh-rsa ./*.ko root@192.168.1.79:~
	scp -oHostKeyAlgorithms=+ssh-rsa out root@192.168.1.79:~
else
	scp -oHostKeyAlgorithms=+ssh-rsa ./*.ko root@192.168.1.162:~
	scp -oHostKeyAlgorithms=+ssh-rsa out root@192.168.1.162:~
fi

make clean
rm out

