#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include <signal.h>

int fd;
int ret[1];
int flag;

static void func(int signam)
{
    read(fd, &ret, sizeof(ret));
    printf("user ----------------> %d\n", ret[0]);
} 

int main(int argc ,char **argv)
{
    fd = open("/dev/second_driver", O_RDWR);
    if(fd < 0) {
        perror("open error: \n");
    }

    signal(SIGIO, func);

    fcntl(fd, F_SETOWN, getpid());
    flag = fcntl(fd, F_GETFD);
    fcntl(fd, F_SETFL, flag | FASYNC);

    while(1){
        // printf("...\n");
        // sleep(3);
    };

    close(fd);
    return 0;
}
