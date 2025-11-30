#include <stdio.h>
#include "timerlib.h"

int timer_open(int fd)
{
    int ret = ioctl(fd, TIME_OPEN_CMD);
    if(ret < 0){
        printf("timer_open error\n");
        return -1;
    }
    return ret;
}