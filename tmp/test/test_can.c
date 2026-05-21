#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#define DEFAULT_IFACE "can0"

/* 发送测试帧：ID 0x100，8 字节数据 0x00~0x07 */
static void send_can_frame(int s, struct sockaddr_can *addr)
{
    struct can_frame frame;
    int nbytes, i;

    /* 填充 CAN 帧 */
    frame.can_id  = 0x100;          /* 标准帧 ID */
    frame.can_dlc = 8;              /* 数据长度 */
    for (i = 0; i < frame.can_dlc; i++) {
        frame.data[i] = i;          /* 数据 0,1,2,...,7 */
    }

    /* 发送 */
    nbytes = sendto(s, &frame, sizeof(frame), 0, (struct sockaddr *)addr, sizeof(*addr));
    if (nbytes < 0) {
        perror("sendto");
        return;
    }
    printf("[TX] ID=0x%03X, DLC=%d, Data:", frame.can_id, frame.can_dlc);
    for (i = 0; i < frame.can_dlc; i++)
        printf(" %02X", frame.data[i]);
    printf("\n");
}

/* 接收并打印 CAN 帧 */
static void recv_can_frame(int s)
{
    struct can_frame frame;
    int nbytes, i;
    struct sockaddr_can addr;
    socklen_t addrlen = sizeof(addr);

    nbytes = recvfrom(s, &frame, sizeof(frame), 0, (struct sockaddr *)&addr, &addrlen);
    if (nbytes < 0) {
        perror("recvfrom");
        return;
    }
    if (nbytes < sizeof(struct can_frame)) {
        fprintf(stderr, "Incomplete CAN frame\n");
        return;
    }

    printf("[RX] ID=0x%03X, DLC=%d, Data:", frame.can_id, frame.can_dlc);
    for (i = 0; i < frame.can_dlc; i++)
        printf(" %02X", frame.data[i]);
    printf("\n");
}

int main(int argc, char **argv)
{
    int s;
    struct sockaddr_can addr;
    struct ifreq ifr;
    const char *iface = DEFAULT_IFACE;
    int mode = 0;   /* 0: send, 1: recv */
    int count = 0;

    /* 解析命令行参数 */
    if (argc >= 2) {
        if (strcmp(argv[1], "send") == 0)
            mode = 0;
        else if (strcmp(argv[1], "recv") == 0)
            mode = 1;
        else {
            fprintf(stderr, "Usage: %s [send|recv] [interface]\n", argv[0]);
            exit(1);
        }
        if (argc >= 3)
            iface = argv[2];
    } else {
        fprintf(stderr, "Usage: %s [send|recv] [interface]\n", argv[0]);
        exit(1);
    }

    /* 创建原始 CAN 套接字 */
    if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("socket");
        exit(1);
    }

    /* 指定 can0 设备 */
    strcpy(ifr.ifr_name, iface);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl SIOCGIFINDEX");
        close(s);
        exit(1);
    }

    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    /* 绑定套接字到 can0 */
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(s);
        exit(1);
    }

    printf("SocketCAN test on %s, mode: %s\n", iface, mode ? "RECV" : "SEND");

    if (mode == 0) {   /* 发送模式 */
        printf("Sending test frames every 1 second. Press Ctrl+C to stop.\n");
        while (1) {
            send_can_frame(s, &addr);
            sleep(1);
        }
    } else {           /* 接收模式 */
        printf("Waiting for CAN frames...\n");
        while (1) {
            recv_can_frame(s);
        }
    }

    close(s);
    return 0;
}