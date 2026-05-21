#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <modbus/modbus.h>

int main(void)
{
    modbus_t *ctx = NULL;
    int rc;
    int i, j;                          /* 循环变量提前定义 */
    uint16_t tab_reg[10];
    int slave_id = 1;                  /* 从机地址，与 Modbus Slave 一致 */
    const char *device = "/dev/ttymxc2";  /* i.MX6ULL 自收发 RS-485 端口 */

    /* 1. 创建 Modbus RTU 主机上下文，9600-8-N-1 */
    ctx = modbus_new_rtu(device, 9600, 'N', 8, 1);
    if (ctx == NULL) {
        fprintf(stderr, "Unable to create libmodbus context\n");
        return -1;
    }

    /* 2. 设置从机地址 */
    modbus_set_slave(ctx, slave_id);

    /* 3. 设置响应超时 1 秒 */
    modbus_set_response_timeout(ctx, 1, 0);

    /* 4. 设置为 RS-485 模式（告知内核这是 485 链路，但禁用 RTS 方向控制）
          因为硬件已实现自收发，无需软件切换方向 */

    /* 5. 连接从机 */
    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }
    printf("Connected to Modbus slave %d on %s (self-driven RS-485)\n", slave_id, device);

    /* 6. 主循环：读取寄存器 0~9，并写寄存器 1 */
    for (i = 0; i < 5; i++) {
        rc = modbus_read_registers(ctx, 0, 10, tab_reg);
        if (rc == -1) {
            fprintf(stderr, "Read error: %s\n", modbus_strerror(errno));
        } else {
            printf("Read %d registers:", rc);
            for (j = 0; j < rc; j++) {
                printf(" %d", tab_reg[j]);
            }
            printf("\n");
        }

        rc = modbus_write_register(ctx, i, (i + 1)*20);
        if (rc == -1) {
            fprintf(stderr, "Write error: %s\n", modbus_strerror(errno));
        } else {
            printf("Write register 1 = %d\n", i + 1);
        }

        sleep(2);
    }

    /* 7. 释放资源 */
    modbus_close(ctx);
    modbus_free(ctx);
    return 0;
}