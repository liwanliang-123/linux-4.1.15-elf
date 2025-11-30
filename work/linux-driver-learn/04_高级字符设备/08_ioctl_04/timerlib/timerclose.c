#include <stdio.h>
#include "timerlib.h"

int timer_close(int fd)
{
    int ret = ioctl(fd, TIME_CLOSE_CMD);
    if(ret < 0){
        printf("timer_close error\n");
        return -1;
    }
    return ret;
}