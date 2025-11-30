#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc ,char **argv)
{
    int fd;
    fd = open("/dev/test",O_RDWR);
    if(fd < 0)
        perror("open error: \n");

    close(fd);
    return 0;
}
