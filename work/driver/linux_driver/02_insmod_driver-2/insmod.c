#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>

/*
    自己实现 insmod 命令 - 方法二
    run: ./app insmod_driver.ko
 */

// 定义在 busybox-1.34.1/modutils/modutils.c
#define init_module(mod, len, opts) syscall(__NR_init_module, mod, len, opts)

int main(int argc, char **argv)
{
    int fd;
    int ret;
    size_t image_size;
	char *image;
    struct stat statbuf;

    fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    if(fd < 0) {
        printf("open error\n");
        return -1;
    }

    fstat(fd, &statbuf);
    image_size = statbuf.st_size;
    image = malloc(image_size);

    read(fd, image, image_size);

    ret = init_module(image, image_size, "");
    if(ret < 0) {
        printf("init_module error\n");
    }
    printf("init_module ok\n");

    close(fd);
    free(image);

    return ret;
}