#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <errno.h>

int main()
{
    int fd;
    fd =  open("/dev/semaphore_misc", O_RDWR);
    if(fd < 0) {
        perror("why");
        printf("/dev/semaphore_misc open faild  fd = %d\n", fd);
        return fd;
    }

    printf("open OK!\n");
    sleep(10);

    close(fd);
    printf("close OK!\n");

    return 0;
}