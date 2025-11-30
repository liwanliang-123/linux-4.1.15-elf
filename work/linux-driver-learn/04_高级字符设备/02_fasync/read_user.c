#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include <fcntl.h>
#include <signal.h>

static int fd;
static int ret;
static char buf_r[32] = { 0 };

static void func(int signam)
{
    read(fd, buf_r, sizeof(buf_r));
    printf("read data = %s\n", buf_r);
} 

int main(int argc ,char **argv)
{
    int flag;

    fd = open("/dev/test", O_RDWR);
    if(fd < 0) {
        perror("open error: \n");
    }

    signal(SIGIO, func);

    fcntl(fd, F_SETOWN, getpid());
    flag = fcntl(fd, F_GETFD);
    fcntl(fd, F_SETFL, flag | FASYNC);

    while(1){
        printf("...\n");
        sleep(3);
    };

    close(fd);

    return 0;
}
