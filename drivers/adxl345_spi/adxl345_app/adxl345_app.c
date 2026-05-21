#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include <linux/input.h>

#define KEY_ON  1
#define KEY_OFF 0

void delay_ms(int ms)
{
    usleep(ms * 1000);
}
int main(void)
{
    int fd = 0;
    int ret = 0;
    short data[3];

    fd = open("/dev/adxl345_misc", O_RDONLY);
    if (-1 == fd)
    {
        printf("open error\n");
        return -1;
    }

    while (1)
    {
        read(fd, data, sizeof(data));
        printf("x:%6d, y:%6d, z:%6d\r", data[0], data[1], data[2]);
        fflush(stdout);
        delay_ms(10);
    }
    
    close(fd);

    return 0;
}
