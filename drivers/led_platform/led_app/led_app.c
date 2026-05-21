#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#define LED_ON  1
#define LED_OFF 0

int main(void)
{
    int fd = 0;
    int stat = LED_OFF;
    int readstat = 0;

    fd = open("/dev/led_misc", O_RDWR);
    if (-1 == fd)
    {
        perror("fail to open");
        return -1;
    }

    while (1)
    {
        stat = LED_ON;
        write(fd, &stat, 4);
        read(fd, &readstat, 4);
        printf("current light stat:%s\n", readstat == LED_ON ? "LED_ON" : "LED_OFF");
        sleep(1);

        stat = LED_OFF;
        write(fd, &stat, 4);
        read(fd, &readstat, 4);
        printf("current light stat:%s\n", readstat == LED_ON ? "LED_ON" : "LED_OFF");
        sleep(1);
    }
    
    close(fd);

    return 0;
}