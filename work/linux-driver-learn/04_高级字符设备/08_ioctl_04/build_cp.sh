#!/bin/bash

set -e

make
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
cd $SCRIPT_DIR/timerlib && ./build.sh && cd $SCRIPT_DIR

if [ $1 -lt 1 ]; then
	scp -oHostKeyAlgorithms=+ssh-rsa ./*.ko root@192.168.1.47:~
	scp -oHostKeyAlgorithms=+ssh-rsa a.out root@192.168.1.47:~
else
	scp -oHostKeyAlgorithms=+ssh-rsa ./*.ko root@192.168.1.162:~
	scp -oHostKeyAlgorithms=+ssh-rsa a.out root@192.168.1.162:~
fi

make clean
rm a.out
