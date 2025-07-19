#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main()
{
    int fd;
    fd =  open("/dev/mutex_misc", O_RDWR);
    if(fd == -1) {
        printf("/dev/mutex_misc open faild\n");
        return fd;
    }

    sleep(8);

    close(fd);

    return 0;
}