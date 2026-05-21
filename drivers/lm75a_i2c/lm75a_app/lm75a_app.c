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
    float temp = 0;
    unsigned short value = 0;

    fd = open("/dev/lm75a_misc", O_RDONLY);
    if (-1 == fd)
    {
        printf("open error\n");
        return -1;
    }

    while (1)
    {
        ret = read(fd, &value, sizeof(value));
        if (ret < 0)
        {
            printf("read error\n");
            break;
        }
        temp = value * 0.5;

        printf("temp = %.1f\n", temp);

        sleep(1);
    }
    
    close(fd);

    return 0;
}
