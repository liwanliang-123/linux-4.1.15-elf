#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include <poll.h>

// write(fd, buf_w, sizeof(buf_w));
// read(fd, buf_r, sizeof(buf_r));
// printf("read data = %s\n", buf_r);

struct pollfd fds;

int main(int argc ,char **argv)
{
    int fd;
    int ret;
    char buf_w[32] = "1111111111";

    fd = open("/dev/test", O_RDWR);
    if(fd < 0) {
        perror("open error: \n");
    }

    printf("write brfore\n");
    write(fd, buf_w, sizeof(buf_w));
    printf("write after\n");

    close(fd);

    return 0;
}
