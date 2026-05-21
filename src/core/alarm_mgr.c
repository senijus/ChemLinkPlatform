#include "alarm_mgr.h"
#include "storage.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#define ALARM_COOLDOWN_SEC 30
#define DEDUP_SLOTS 64

static AlarmList_t alarm_list;
static AlarmThreshold_t threshold = {
    .tem_max = 80.0, .tem_min = 0.0,
    .hum_max = 90.0, .hum_min = 0.0,
    .gas_max = 80.0, .gas_min = 0.0,
    .acc_max = 80.0, .acc_min = 0.0,
    .rs485_volt_max = 380.0, .rs485_volt_min = 0.0,
    .rs485_elec_max = 100.0, .rs485_elec_min = 0.0,
    .can_speed_max = 5000.0, .can_speed_min = 0.0,
    .can_flow_max = 200.0, .can_flow_min = 0.0
};

// 告警去重：同类型+同来源 30s 内不重复触发
typedef struct {
    int  type;
    char source[32];
    time_t last_ts;
} DedupSlot_t;

static DedupSlot_t g_dedup[DEDUP_SLOTS];
static int g_dedup_count = 0;

static int dedup_check(int type, const char *source)
{
    time_t now = time(NULL);
    for (int i = 0; i < g_dedup_count; i++) {
        if (g_dedup[i].type == type &&
            strcmp(g_dedup[i].source, source) == 0) {
            if (now - g_dedup[i].last_ts < ALARM_COOLDOWN_SEC)
                return 1;  // 冷却中，跳过
            g_dedup[i].last_ts = now;
            return 0;
        }
    }
    // 未命中，新增槽位
    if (g_dedup_count < DEDUP_SLOTS) {
        g_dedup[g_dedup_count].type = type;
        strncpy(g_dedup[g_dedup_count].source, source,
                sizeof(g_dedup[0].source) - 1);
        g_dedup[g_dedup_count].source[sizeof(g_dedup[0].source) - 1] = '\0';
        g_dedup[g_dedup_count].last_ts = now;
        g_dedup_count++;
    }
    return 0;
}

static const char *type_names[] = {
    "Temperature High", "Humidity High", "Gas High", "Acceleration High",
    "RS485 Voltage High", "RS485 Current High", "CAN Speed High", "CAN Flow High",
    "Temperature Low", "Humidity Low", "Gas Low", "Acceleration Low",
    "RS485 Voltage Low", "RS485 Current Low", "CAN Speed Low", "CAN Flow Low"
};

static const char *type_units[] = {
    "C", "%", "C", "g", "V", "A", "rpm", "L/min",
    "C", "%", "C", "g", "V", "A", "rpm", "L/min"
};

const char *AlarmMgrTypeName(int type)
{
    if (type >= 0 && type < ALARM_TYPE_COUNT)
        return type_names[type];
    return "Unknown";
}

int AlarmMgrFormatDesc(const AlarmRecord_t *rec, char *buf, int buf_size)
{
    if (rec == NULL || buf == NULL || buf_size <= 0)
        return -1;

    const char *type_name = (rec->type >= 0 && rec->type < ALARM_TYPE_COUNT)
                            ? type_names[rec->type] : "Unknown";
    const char *unit = (rec->type >= 0 && rec->type < ALARM_TYPE_COUNT)
                       ? type_units[rec->type] : "";

    int is_low = (rec->type >= ALARM_TYPE_TEM_LOW);
    return snprintf(buf, buf_size, "[%s] %s: %.2f%s %s threshold %.2f%s",
                    rec->source, type_name, rec->value, unit,
                    is_low ? "<" : ">",
                    rec->threshold, unit);
}

static void add_record(int type, const char *source, double value, double thr)
{
    if (dedup_check(type, source))
        return;

    if (alarm_list.count >= ALARM_MAX_RECORDS)
    {
        for (int i = 0; i < ALARM_MAX_RECORDS - 1; i++)
            alarm_list.records[i] = alarm_list.records[i + 1];
        alarm_list.count = ALARM_MAX_RECORDS - 1;
    }

    AlarmRecord_t *rec = &alarm_list.records[alarm_list.count];
    rec->type = type;
    strncpy(rec->source, source ? source : "Local", sizeof(rec->source) - 1);
    rec->source[sizeof(rec->source) - 1] = '\0';
    rec->value = value;
    rec->threshold = thr;
    rec->confirmed = 0;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    snprintf(rec->timestamp, sizeof(rec->timestamp),
             "%04d-%02d-%02d %02d:%02d:%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

    // 持久化到数据库
    StorageInsertAlarm(rec->timestamp, source ? source : "Local",
                       AlarmMgrTypeName(type), value, thr);

    alarm_list.count++;
    alarm_list.unread++;
    alarm_list.version++;
}

int AlarmMgrInit(void)
{
    memset(&alarm_list, 0, sizeof(alarm_list));
    memset(g_dedup, 0, sizeof(g_dedup));
    g_dedup_count = 0;

    StorageLoadThreshold(&threshold.tem_max, &threshold.tem_min,
                         &threshold.hum_max, &threshold.hum_min,
                         &threshold.gas_max, &threshold.gas_min,
                         &threshold.acc_max, &threshold.acc_min,
                         &threshold.rs485_volt_max, &threshold.rs485_volt_min,
                         &threshold.rs485_elec_max, &threshold.rs485_elec_min,
                         &threshold.can_speed_max, &threshold.can_speed_min,
                         &threshold.can_flow_max, &threshold.can_flow_min);

    return 0;
}

int AlarmMgrCheck(const Data_t *data)
{
    int triggered = 0;

    // 本地传感器 — 上限
    if (data->Tem > threshold.tem_max) {
        add_record(ALARM_TYPE_TEM_HIGH, "Local", data->Tem, threshold.tem_max);
        triggered = 1;
    }
    if (data->Tem < threshold.tem_min) {
        add_record(ALARM_TYPE_TEM_LOW, "Local", data->Tem, threshold.tem_min);
        triggered = 1;
    }
    if (data->Hum > threshold.hum_max) {
        add_record(ALARM_TYPE_HUM_HIGH, "Local", data->Hum, threshold.hum_max);
        triggered = 1;
    }
    if (data->Hum < threshold.hum_min) {
        add_record(ALARM_TYPE_HUM_LOW, "Local", data->Hum, threshold.hum_min);
        triggered = 1;
    }
    if (data->Gas > threshold.gas_max) {
        add_record(ALARM_TYPE_GAS_HIGH, "Local", data->Gas, threshold.gas_max);
        triggered = 1;
    }
    if (data->Gas < threshold.gas_min) {
        add_record(ALARM_TYPE_GAS_LOW, "Local", data->Gas, threshold.gas_min);
        triggered = 1;
    }
    if (data->Acc_xyz > threshold.acc_max) {
        add_record(ALARM_TYPE_ACC_HIGH, "Local", data->Acc_xyz, threshold.acc_max);
        triggered = 1;
    }
    if (data->Acc_xyz < threshold.acc_min) {
        add_record(ALARM_TYPE_ACC_LOW, "Local", data->Acc_xyz, threshold.acc_min);
        triggered = 1;
    }

    // RS485 设备
    for (int i = 0; i < data->rs485_count; i++) {
        char source[32];
        snprintf(source, sizeof(source), "RS485-Dev%d", data->rs485_dev[i].id);

        if (data->rs485_dev[i].Volt > threshold.rs485_volt_max) {
            add_record(ALARM_TYPE_RS485_VOLT_HIGH, source,
                       data->rs485_dev[i].Volt, threshold.rs485_volt_max);
            triggered = 1;
        }
        if (data->rs485_dev[i].Volt < threshold.rs485_volt_min) {
            add_record(ALARM_TYPE_RS485_VOLT_LOW, source,
                       data->rs485_dev[i].Volt, threshold.rs485_volt_min);
            triggered = 1;
        }
        if (data->rs485_dev[i].Elec > threshold.rs485_elec_max) {
            add_record(ALARM_TYPE_RS485_ELEC_HIGH, source,
                       data->rs485_dev[i].Elec, threshold.rs485_elec_max);
            triggered = 1;
        }
        if (data->rs485_dev[i].Elec < threshold.rs485_elec_min) {
            add_record(ALARM_TYPE_RS485_ELEC_LOW, source,
                       data->rs485_dev[i].Elec, threshold.rs485_elec_min);
            triggered = 1;
        }
    }

    // CAN 设备
    for (int i = 0; i < data->can_count; i++) {
        char source[32];
        snprintf(source, sizeof(source), "CAN-Dev%d", data->can_dev[i].id);

        if (data->can_dev[i].Speed > threshold.can_speed_max) {
            add_record(ALARM_TYPE_CAN_SPEED_HIGH, source,
                       data->can_dev[i].Speed, threshold.can_speed_max);
            triggered = 1;
        }
        if (data->can_dev[i].Speed < threshold.can_speed_min) {
            add_record(ALARM_TYPE_CAN_SPEED_LOW, source,
                       data->can_dev[i].Speed, threshold.can_speed_min);
            triggered = 1;
        }
        if (data->can_dev[i].Flow > threshold.can_flow_max) {
            add_record(ALARM_TYPE_CAN_FLOW_HIGH, source,
                       data->can_dev[i].Flow, threshold.can_flow_max);
            triggered = 1;
        }
        if (data->can_dev[i].Flow < threshold.can_flow_min) {
            add_record(ALARM_TYPE_CAN_FLOW_LOW, source,
                       data->can_dev[i].Flow, threshold.can_flow_min);
            triggered = 1;
        }
    }

    return triggered;
}

const AlarmList_t *AlarmMgrGetList(void)
{
    return &alarm_list;
}

int AlarmMgrConfirm(int index)
{
    if (index < 0 || index >= alarm_list.count)
        return -1;

    alarm_list.records[index].confirmed = 1;
    if (alarm_list.unread > 0)
        alarm_list.unread--;
    alarm_list.version++;
    return 0;
}

int AlarmMgrClearAll(void)
{
    alarm_list.count = 0;
    alarm_list.unread = 0;
    alarm_list.version++;
    memset(g_dedup, 0, sizeof(g_dedup));
    g_dedup_count = 0;
    return 0;
}

int AlarmMgrGetUnreadCount(void)
{
    return alarm_list.unread;
}

const AlarmThreshold_t *AlarmMgrGetThreshold(void)
{
    return &threshold;
}

void AlarmMgrSetThreshold(const AlarmThreshold_t *thr)
{
    if (thr != NULL)
    {
        threshold = *thr;
        memset(g_dedup, 0, sizeof(g_dedup));
        g_dedup_count = 0;
        StorageSaveThreshold(thr->tem_max, thr->tem_min,
                             thr->hum_max, thr->hum_min,
                             thr->gas_max, thr->gas_min,
                             thr->acc_max, thr->acc_min,
                             thr->rs485_volt_max, thr->rs485_volt_min,
                             thr->rs485_elec_max, thr->rs485_elec_min,
                             thr->can_speed_max, thr->can_speed_min,
                             thr->can_flow_max, thr->can_flow_min);
    }
}
