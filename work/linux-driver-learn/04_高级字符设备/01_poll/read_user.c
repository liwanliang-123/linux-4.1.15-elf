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
    char buf_w[32] = "nihao";
    char buf_r[32] = { 0 };

    fd = open("/dev/test", O_RDWR);
    if(fd < 0) {
        perror("open error: \n");
    }

    fds.fd = fd;
    fds.events = POLLIN;  //读事件

    while (1) {
        ret = poll(&fds, 1, 3000);
        if(!ret) {
            printf("poll timeout!\n");
        }

// 我们需要的读事件, 相当于是当内核设置这个标志之后才会进入这个 if 中去
// 然后再将数据读出来，不然一直调用不到 read 函数
// 表示已经有读的事件了，所以进去读数据
        if(fds.revents == POLLIN) {    
            read(fd, buf_r, sizeof(buf_r));
            printf("read data = %s\n", buf_r);
            sleep(1);
            break;
        }
    }

    printf("poll user end!\n");
    close(fd);

    return 0;
}
