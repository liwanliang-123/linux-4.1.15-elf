#include <stdio.h>
#include "timerlib.h"

int timer_set(int fd, int arg)
{
    int ret = ioctl(fd, TIME_SET_TIMER_CMD, arg);
    if(ret < 0){
        printf("timer_set error\n");
        return -1;
    }
    return ret;
}