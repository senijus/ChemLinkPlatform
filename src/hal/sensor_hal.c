#include "sensor_hal.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define DHT11_DEV    "/dev/dht11_misc"
#define ADXL345_DEV  "/dev/adxl345_misc"
#define LM75A_DEV    "/dev/lm75a_misc"
#define LED_DEV      "/dev/led_misc"
#define BEEP_DEV     "/dev/beep_misc"

static int g_led_state = 0;
static int g_beep_state = 0;

int SensorHalInit(void)
{
    // 检查设备节点是否存在
    if (access(DHT11_DEV, F_OK) != 0)
        fprintf(stderr, "SensorHalInit: %s not found\n", DHT11_DEV);
    if (access(ADXL345_DEV, F_OK) != 0)
        fprintf(stderr, "SensorHalInit: %s not found\n", ADXL345_DEV);
    if (access(LM75A_DEV, F_OK) != 0)
        fprintf(stderr, "SensorHalInit: %s not found\n", LM75A_DEV);
    if (access(LED_DEV, F_OK) != 0)
        fprintf(stderr, "SensorHalInit: %s not found\n", LED_DEV);
    if (access(BEEP_DEV, F_OK) != 0)
        fprintf(stderr, "SensorHalInit: %s not found\n", BEEP_DEV);
    return 0;
}

void SensorHalDeinit(void)
{
    LedControl(0);
    BeepControl(0);
}

int Dht11Read(double *temperature, double *humidity)
{
    unsigned char buf[5] = {0};
    int fd = open(DHT11_DEV, O_RDONLY);
    if (fd < 0) {
        static int dht11_warned = 0;
        if (!dht11_warned) {
            char log_buf[128] = {0};
            snprintf(log_buf, sizeof(log_buf), "Dht11Read: open %s failed: %s", DHT11_DEV, strerror(errno));
            LogWrite(LOG_LEVEL_WARN, log_buf);
            dht11_warned = 1;
        }
        return -1;
    }

    int ret = read(fd, buf, sizeof(buf));
    close(fd);

    if (ret != 5) {
        LogWrite(LOG_LEVEL_WARN, "Dht11Read: read returned wrong size");
        return -1;
    }

    // 校验和：buf[4] == buf[0]+buf[1]+buf[2]+buf[3]
    unsigned char sum = buf[0] + buf[1] + buf[2] + buf[3];
    if (sum != buf[4]) {
        LogWrite(LOG_LEVEL_WARN, "Dht11Read: checksum mismatch");
        return -1;
    }

    *humidity    = buf[0] + buf[1] / 10.0;
    *temperature = buf[2] + buf[3] / 10.0;
    return 0;
}

int Adxl345Read(double *acc_xyz)
{
    short xyz[3] = {0};
    int fd = open(ADXL345_DEV, O_RDONLY);
    if (fd < 0) {
        static int adxl345_warned = 0;
        if (!adxl345_warned) {
            char log_buf[128] = {0};
            snprintf(log_buf, sizeof(log_buf), "Adxl345Read: open %s failed: %s", ADXL345_DEV, strerror(errno));
            LogWrite(LOG_LEVEL_WARN, log_buf);
            adxl345_warned = 1;
        }
        return -1;
    }

    int ret = read(fd, xyz, sizeof(xyz));
    close(fd);

    if (ret != sizeof(xyz)) {
        LogWrite(LOG_LEVEL_WARN, "Adxl345Read: read returned wrong size");
        return -1;
    }

    // ADXL345 默认量程 ±2g，分辨率 3.9 mg/LSB
    double x = xyz[0] * 3.9 / 1000.0;
    double y = xyz[1] * 3.9 / 1000.0;
    double z = xyz[2] * 3.9 / 1000.0;
    *acc_xyz = sqrt(x * x + y * y + z * z);
    return 0;
}

int Lm75aRead(double *gas_value)
{
    unsigned short raw = 0;
    int fd = open(LM75A_DEV, O_RDONLY);
    if (fd < 0) {
        static int lm75a_warned = 0;
        if (!lm75a_warned) {
            char log_buf[128] = {0};
            snprintf(log_buf, sizeof(log_buf), "Lm75aRead: open %s failed: %s", LM75A_DEV, strerror(errno));
            LogWrite(LOG_LEVEL_WARN, log_buf);
            lm75a_warned = 1;
        }
        return -1;
    }

    int ret = read(fd, &raw, sizeof(raw));
    close(fd);

    if (ret != sizeof(raw)) {
        LogWrite(LOG_LEVEL_WARN, "Lm75aRead: read returned wrong size");
        return -1;
    }

    // 内核驱动已对寄存器值做了 >> 7，此处只需 * 0.5 得到温度 (°C)
    double temp = raw * 0.5;
    *gas_value = temp;
    return 0;
}

static int write_int_to_dev(const char *dev_path, int value)
{
    int fd = open(dev_path, O_WRONLY);
    if (fd < 0) return -1;

    int ret = write(fd, &value, sizeof(value));
    close(fd);
    return (ret == sizeof(value)) ? 0 : -1;
}

int LedControl(int on_off)
{
    int ret = write_int_to_dev(LED_DEV, on_off ? 1 : 0);
    if (ret == 0) g_led_state = on_off ? 1 : 0;
    return ret;
}

int BeepControl(int on_off)
{
    int ret = write_int_to_dev(BEEP_DEV, on_off ? 1 : 0);
    if (ret == 0) g_beep_state = on_off ? 1 : 0;
    return ret;
}

int LedGetState(void)
{
    return g_led_state;
}

int BeepGetState(void)
{
    return g_beep_state;
}
