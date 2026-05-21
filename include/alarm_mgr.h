#ifndef __ALARM_MGR_H__
#define __ALARM_MGR_H__

#include "mailbox.h"

#define ALARM_MAX_RECORDS 100

typedef enum {
    // 上限告警
    ALARM_TYPE_TEM_HIGH = 0,
    ALARM_TYPE_HUM_HIGH,
    ALARM_TYPE_GAS_HIGH,
    ALARM_TYPE_ACC_HIGH,
    ALARM_TYPE_RS485_VOLT_HIGH,
    ALARM_TYPE_RS485_ELEC_HIGH,
    ALARM_TYPE_CAN_SPEED_HIGH,
    ALARM_TYPE_CAN_FLOW_HIGH,
    // 下限告警
    ALARM_TYPE_TEM_LOW,
    ALARM_TYPE_HUM_LOW,
    ALARM_TYPE_GAS_LOW,
    ALARM_TYPE_ACC_LOW,
    ALARM_TYPE_RS485_VOLT_LOW,
    ALARM_TYPE_RS485_ELEC_LOW,
    ALARM_TYPE_CAN_SPEED_LOW,
    ALARM_TYPE_CAN_FLOW_LOW,
    ALARM_TYPE_COUNT
} AlarmType_t;

typedef struct {
    int type;
    char source[32];
    char timestamp[24];
    double value;
    double threshold;
    int confirmed;
} AlarmRecord_t;

typedef struct {
    AlarmRecord_t records[ALARM_MAX_RECORDS];
    int count;
    int unread;
    int version;   // 递增版本号，UI 据此判断是否需要刷新
} AlarmList_t;

typedef struct {
    double tem_max, tem_min;
    double hum_max, hum_min;
    double gas_max, gas_min;
    double acc_max, acc_min;
    double rs485_volt_max, rs485_volt_min;
    double rs485_elec_max, rs485_elec_min;
    double can_speed_max, can_speed_min;
    double can_flow_max, can_flow_min;
} AlarmThreshold_t;

int AlarmMgrInit(void);
int AlarmMgrCheck(const Data_t *data);
const AlarmList_t *AlarmMgrGetList(void);
int AlarmMgrConfirm(int index);
int AlarmMgrClearAll(void);
int AlarmMgrGetUnreadCount(void);

const AlarmThreshold_t *AlarmMgrGetThreshold(void);
void AlarmMgrSetThreshold(const AlarmThreshold_t *thr);

const char *AlarmMgrTypeName(int type);

int AlarmMgrFormatDesc(const AlarmRecord_t *rec, char *buf, int buf_size);

#endif
