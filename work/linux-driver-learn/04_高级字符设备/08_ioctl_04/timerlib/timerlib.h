#ifndef __TIMER_LIB_H__
#define __TIMER_LIB_H__

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/ioctl.h>

#define TIME_OPEN_CMD _IO('a', 0)
#define TIME_SET_TIMER_CMD _IOW('a', 1, int)
#define TIME_CLOSE_CMD _IO('a', 2)

int timer_open(int fd);
int timer_set(int fd, int arg);
int timer_close(int fd);

#endif