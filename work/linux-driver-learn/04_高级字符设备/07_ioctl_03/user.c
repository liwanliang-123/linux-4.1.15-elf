#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/ioctl.h>

#define TIME_OPEN_CMD _IO('a', 0)
#define TIME_SET_TIMER_CMD _IOW('a', 1, int)
#define TIME_CLOSE_CMD _IO('a', 2)

int main(int argc ,char **argv)
{
    int fd;
    int ret;

    fd = open("/dev/ioctl_driver", O_RDWR);
    if(fd < 0) {
        perror("open error: \n");
    }

    ioctl(fd, TIME_SET_TIMER_CMD, 1000);
    ioctl(fd, TIME_OPEN_CMD);

    sleep(5);

    ioctl(fd, TIME_CLOSE_CMD);

    return 0;
}
