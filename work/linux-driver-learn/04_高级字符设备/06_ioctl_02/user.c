#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/ioctl.h>

#define CMD_TEST0 _IO('a', 0)
#define CMD_TEST1 _IOW('a', 1, int)
#define CMD_TEST2 _IOR('a', 2, int)

struct args {
    int a;
    int b;
    int c;
};

int main(int argc ,char **argv)
{
    int fd;
    fd = open("/dev/ioctl_driver", O_RDWR);
    if(fd < 0) {
        perror("open error: \n");
    }

    struct args test;
    test.a = 1;
    test.b = 2;
    test.c = 3;

    while (1) {
        ioctl(fd, CMD_TEST1, &test);
        sleep(1);
    }

    return 0;
}
