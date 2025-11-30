#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc ,char **argv)
{
    int fd;
    int ret;
    char buf_w[32] = "hello world\n";

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
