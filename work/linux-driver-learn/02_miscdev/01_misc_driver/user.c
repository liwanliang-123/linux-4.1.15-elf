#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc ,char **argv)
{
    int fd;
    char buf_w[32] = "nihao";
    char buf_r[32] = { 0 };

    fd = open("/dev/test",O_RDWR);
    if(fd < 0) {
        perror("open error: \n");
    }

    write(fd, buf_w, sizeof(buf_w));

    read(fd, buf_r, sizeof(buf_r));
    printf("read data = %s\n", buf_r);

    close(fd);
    return 0;
}
