#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/ioctl.h>

#include "timerlib.h"

int main(int argc ,char **argv)
{
    int fd;

    fd = open("/dev/ioctl_driver", O_RDWR);
    if(fd < 0) {
        perror("open error: \n");
    }

    timer_set(fd, 1000);
    timer_open(fd);

    sleep(5);

    timer_close(fd);

    return 0;
}
