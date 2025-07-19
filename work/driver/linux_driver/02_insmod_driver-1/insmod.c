#include <stdio.h>

#include <fcntl.h>
#include <unistd.h>

#include <sys/syscall.h>

/*
    自己实现 insmod 命令 - 方法一
    run: ./app insmod_driver.ko
 */

// 定义在 busybox-1.34.1/modutils/modutils.c
# define finit_module(fd, uargs, flags) syscall(__NR_finit_module, fd, uargs, flags)

int main(int argc, char **argv)
{
    int fd;
    int ret;

    fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    if(fd < 0) {
        printf("open error\n");
        return -1;
    }

    ret = finit_module(fd, "", 0);

    close(fd);
    return ret;
}