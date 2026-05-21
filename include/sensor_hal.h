#ifndef __SENSOR_HAL_H__
#define __SENSOR_HAL_H__

// 初始化/反初始化（打开/关闭设备节点 fd）
int SensorHalInit(void);
void SensorHalDeinit(void);

// 传感器读取
int Dht11Read(double *temperature, double *humidity);
int Adxl345Read(double *acc_xyz);    // 返回合成加速度 (g)
int Lm75aRead(double *gas_value);    // LM75A 温度直接作为 Gas 值 (°C)

// LED / BEEP 控制
int LedControl(int on_off);          // 1=ON, 0=OFF
int BeepControl(int on_off);
int LedGetState(void);
int BeepGetState(void);

#endif
