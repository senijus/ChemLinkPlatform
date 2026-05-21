#ifndef __DEV_MGR_H__
#define __DEV_MGR_H__

// RS485 设备管理（供 UI 调用）
int Rs485AddDevice(int dev_id);
int Rs485RemoveDevice(int dev_id);
int Rs485GetDevices(int *ids, int *count);

// CAN 设备管理（供 UI 调用）
int CanAddDevice(int can_id);
int CanRemoveDevice(int can_id);
int CanGetDevices(int *ids, int *count);

#endif
