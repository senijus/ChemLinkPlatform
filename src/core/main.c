#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <modbus/modbus.h>

#include "storage.h"
#include "mailbox.h"
#include "log.h"
#include "mqtt.h"
#include "lvgl_ui.h"
#include "alarm_mgr.h"
#include "sensor_hal.h"
#include "dev_mgr.h"

#define RS485_DEVICE    "/dev/ttymxc2"
#define RS485_BAUD      9600
#define CAN_INTERFACE   "can0"

// ============ RS485 动态设备列表 ============
static int g_rs485_ids[RS485_MAX_DEV];
static int g_rs485_count = 0;
static pthread_mutex_t g_rs485_lock = PTHREAD_MUTEX_INITIALIZER;

// ============ CAN 动态设备列表 ============
static int g_can_ids[CAN_MAX_DEV];
static int g_can_count = 0;
static pthread_mutex_t g_can_lock = PTHREAD_MUTEX_INITIALIZER;

// ============ RS485 实时数据缓冲（RS485 线程写，collect 线程读） ============
static Rs485Device_t g_rs485_buf[RS485_MAX_DEV];
static int g_rs485_buf_count = 0;
static pthread_mutex_t g_rs485_buf_lock = PTHREAD_MUTEX_INITIALIZER;

// ============ CAN 实时数据缓冲（CAN 线程写，collect 线程读） ============
static CanDevice_t g_can_buf[CAN_MAX_DEV];
static int g_can_buf_count = 0;
static pthread_mutex_t g_can_buf_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_t lvgl_tid;

// ============ 本地传感器采集线程（统一聚合 + 分时发送） ============

static void *pthread_collect(void *arg)
{
    Data_t tmpdata;
    memset(&tmpdata, 0, sizeof(tmpdata));
    int cycle = 0;

    SensorHalInit();
    sleep(2);

    while(1)
    {
        memset(&tmpdata, 0, sizeof(tmpdata));

        // 1. 读取本地传感器
        double tem = 0, hum = 0, gas = 0, acc = 0;
        int dht_ok = (Dht11Read(&tem, &hum) == 0);
        int lm_ok  = (Lm75aRead(&gas) == 0);
        int adx_ok = (Adxl345Read(&acc) == 0);

        if (dht_ok) { tmpdata.Tem = tem; tmpdata.Hum = hum; }
        if (lm_ok)  { tmpdata.Gas = gas; }
        if (adx_ok) { tmpdata.Acc_xyz = acc; }

        // 首次采集打印传感器状态
        if (cycle == 0) {
            char sbuf[160] = {0};
            snprintf(sbuf, sizeof(sbuf),
                     "Sensor status: DHT11=%s Tem=%.1f Hum=%.1f | LM75A=%s Gas=%.1f | ADXL345=%s Acc=%.3f",
                     dht_ok ? "OK" : "FAIL", tem, hum,
                     lm_ok  ? "OK" : "FAIL", gas,
                     adx_ok ? "OK" : "FAIL", acc);
            LogWrite(LOG_LEVEL_INFO, sbuf);
        }

        // 2. 加锁拷贝 RS485 实时数据缓冲
        pthread_mutex_lock(&g_rs485_buf_lock);
        tmpdata.rs485_count = g_rs485_buf_count;
        memcpy(tmpdata.rs485_dev, g_rs485_buf,
               sizeof(Rs485Device_t) * g_rs485_buf_count);
        pthread_mutex_unlock(&g_rs485_buf_lock);

        // 3. 加锁拷贝 CAN 实时数据缓冲
        pthread_mutex_lock(&g_can_buf_lock);
        tmpdata.can_count = g_can_buf_count;
        memcpy(tmpdata.can_dev, g_can_buf,
               sizeof(CanDevice_t) * g_can_buf_count);
        pthread_mutex_unlock(&g_can_buf_lock);

        // 4. LED/BEEP 告警联动
        {
            const AlarmThreshold_t *thr = AlarmMgrGetThreshold();
            int any_alarm = 0;

            if (tmpdata.Tem > thr->tem_max || tmpdata.Tem < thr->tem_min) any_alarm = 1;
            if (tmpdata.Hum > thr->hum_max || tmpdata.Hum < thr->hum_min) any_alarm = 1;
            if (tmpdata.Gas > thr->gas_max || tmpdata.Gas < thr->gas_min) any_alarm = 1;
            if (tmpdata.Acc_xyz > thr->acc_max || tmpdata.Acc_xyz < thr->acc_min) any_alarm = 1;

            for (int i = 0; i < tmpdata.rs485_count && !any_alarm; i++) {
                if (tmpdata.rs485_dev[i].Volt > thr->rs485_volt_max ||
                    tmpdata.rs485_dev[i].Volt < thr->rs485_volt_min) any_alarm = 1;
                if (tmpdata.rs485_dev[i].Elec > thr->rs485_elec_max ||
                    tmpdata.rs485_dev[i].Elec < thr->rs485_elec_min) any_alarm = 1;
            }
            for (int i = 0; i < tmpdata.can_count && !any_alarm; i++) {
                if (tmpdata.can_dev[i].Speed > thr->can_speed_max ||
                    tmpdata.can_dev[i].Speed < thr->can_speed_min) any_alarm = 1;
                if (tmpdata.can_dev[i].Flow > thr->can_flow_max ||
                    tmpdata.can_dev[i].Flow < thr->can_flow_min) any_alarm = 1;
            }

            LedControl(any_alarm ? 1 : 0);
            BeepControl(any_alarm ? 1 : 0);
        }

        // 5. 分时发送
        // 每次 → 发送给 lvgl（实时显示）
        MailBoxSendMsg("lvgl", tmpdata);

        // 每 3 次（6 秒）→ 发送给 storage（批量写入）
        if (cycle % 3 == 0) {
            MailBoxSendMsg("storage", tmpdata);
        }

        // 每 5 次（10 秒）→ 发送给 mqtt
        if (cycle % 5 == 0) {
            MailBoxSendMsg("mqtt", tmpdata);
        }

        cycle++;
        sleep(2);
    }

    SensorHalDeinit();
    return 0;
}

// ============ RS485 采集线程（只更新全局缓冲，不发邮箱） ============

static void *pthread_rs485(void *arg)
{
    modbus_t *ctx = NULL;
    uint16_t regs[4];
    int ret;

    ctx = modbus_new_rtu(RS485_DEVICE, RS485_BAUD, 'N', 8, 1);
    if (ctx == NULL) {
        LogWrite(LOG_LEVEL_ERROR, "RS485: failed to create modbus context");
        return NULL;
    }

    modbus_set_response_timeout(ctx, 1, 0);

    if (modbus_connect(ctx) == -1) {
        LogWrite(LOG_LEVEL_ERROR, "RS485: connection failed");
        modbus_free(ctx);
        return NULL;
    }

    LogWrite(LOG_LEVEL_INFO, "RS485: connected");

    while (1)
    {
        pthread_mutex_lock(&g_rs485_lock);
        int count = g_rs485_count;
        int ids[RS485_MAX_DEV];
        memcpy(ids, g_rs485_ids, sizeof(int) * count);
        pthread_mutex_unlock(&g_rs485_lock);

        for (int i = 0; i < count; i++)
        {
            modbus_set_slave(ctx, ids[i]);

            ret = modbus_read_registers(ctx, 0, 4, regs);
            if (ret == -1) {
                char log_buf[128] = {0};
                snprintf(log_buf, sizeof(log_buf), "RS485: read slave %d failed: %s",
                         ids[i], modbus_strerror(errno));
                LogWrite(LOG_LEVEL_ERROR, log_buf);
                continue;
            }

            double volt = regs[0] + regs[1] / 100.0;
            double elec = regs[2] + regs[3] / 100.0;

            // 在全局缓冲中查找并原地更新（不替换整个缓冲，保留离线设备旧值）
            pthread_mutex_lock(&g_rs485_buf_lock);
            int found = 0;
            for (int j = 0; j < g_rs485_buf_count; j++) {
                if (g_rs485_buf[j].id == ids[i]) {
                    g_rs485_buf[j].Volt = volt;
                    g_rs485_buf[j].Elec = elec;
                    found = 1;
                    break;
                }
            }
            if (!found && g_rs485_buf_count < RS485_MAX_DEV) {
                g_rs485_buf[g_rs485_buf_count].id = ids[i];
                g_rs485_buf[g_rs485_buf_count].Volt = volt;
                g_rs485_buf[g_rs485_buf_count].Elec = elec;
                g_rs485_buf_count++;
            }
            pthread_mutex_unlock(&g_rs485_buf_lock);
        }

        sleep(5);
    }

    modbus_close(ctx);
    modbus_free(ctx);
    return NULL;
}

// ============ CAN 监听线程（只更新全局缓冲，不发邮箱） ============

static void *pthread_can(void *arg)
{
    int s;
    struct sockaddr_can addr;
    struct ifreq ifr;
    struct can_frame frame;
    int nbytes;

    s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        LogWrite(LOG_LEVEL_ERROR, "CAN: socket create failed");
        return NULL;
    }

    strcpy(ifr.ifr_name, CAN_INTERFACE);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        LogWrite(LOG_LEVEL_ERROR, "CAN: ioctl failed");
        close(s);
        return NULL;
    }

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LogWrite(LOG_LEVEL_ERROR, "CAN: bind failed");
        close(s);
        return NULL;
    }

    // 设置接收超时 1 秒，避免 recvfrom 永久阻塞
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    LogWrite(LOG_LEVEL_INFO, "CAN: listening on " CAN_INTERFACE);

    while (1)
    {
        nbytes = recvfrom(s, &frame, sizeof(frame), 0, NULL, NULL);
        if (nbytes < sizeof(struct can_frame)) {
            continue;
        }

        // 忽略错误帧和远程帧
        if (frame.can_id & CAN_ERR_FLAG) continue;
        if (frame.can_id & CAN_RTR_FLAG) continue;

        // 提取标准帧 ID（去除 EFF/RTR/ERR 标志位）
        int frame_id;
        if (frame.can_id & CAN_EFF_FLAG)
            frame_id = frame.can_id & CAN_EFF_MASK;
        else
            frame_id = frame.can_id & CAN_SFF_MASK;

        // 检查这个 CAN ID 是否在注册列表中
        int found = 0;
        pthread_mutex_lock(&g_can_lock);
        for (int i = 0; i < g_can_count; i++) {
            if (g_can_ids[i] == frame_id) {
                found = 1;
                break;
            }
        }
        pthread_mutex_unlock(&g_can_lock);

        if (!found) continue;

        // 解析 CAN 帧
        int speed_int = (frame.data[0] << 16) | (frame.data[1] << 8) | frame.data[2];
        int speed_frac = frame.data[3];
        int flow_int = (frame.data[4] << 16) | (frame.data[5] << 8) | frame.data[6];
        int flow_frac = frame.data[7];
        double speed = speed_int + speed_frac / 100.0;
        double flow = flow_int + flow_frac / 100.0;

        // 更新全局缓冲（查找预分配条目更新，或新增）
        pthread_mutex_lock(&g_can_buf_lock);
        int updated = 0;
        for (int i = 0; i < g_can_buf_count; i++) {
            if (g_can_buf[i].id == frame_id) {
                g_can_buf[i].Speed = speed;
                g_can_buf[i].Flow = flow;
                updated = 1;
                break;
            }
        }
        if (!updated && g_can_buf_count < CAN_MAX_DEV) {
            g_can_buf[g_can_buf_count].id = frame_id;
            g_can_buf[g_can_buf_count].Speed = speed;
            g_can_buf[g_can_buf_count].Flow = flow;
            g_can_buf_count++;
            char log_buf[80] = {0};
            snprintf(log_buf, sizeof(log_buf), "CAN: rx ID=0x%X spd=%.1f flow=%.1f",
                     frame_id, speed, flow);
            LogWrite(LOG_LEVEL_INFO, log_buf);
        }
        pthread_mutex_unlock(&g_can_buf_lock);
    }

    close(s);
    return NULL;
}

// ============ 存储线程（批量写入） ============

static void *pthread_storage(void *arg)
{
    Data_t data = {0};
    int cycle = 0;

    StorageInit("info.db");

    while(1)
    {
        MailBoxRecvMsg(&data);
        StorageBatchInsert(&data);

        // 每 30 次（约 60 秒）清理一次过期数据和 WAL checkpoint
        cycle++;
        if (cycle % 30 == 0) {
            StorageCleanOldData(1);
            StorageCheckpoint();
        }
    }

    StorageDeInit();
    return 0;
}

// ============ MQTT 线程（断网续传，重连在主循环内完成） ============

#define RESUME_BATCH_SIZE 10

// 分批续传：每次取 RESUME_BATCH_SIZE 条，发送成功后逐批标记
static void resume_unsynced_data(void)
{
    DataEntry_t *batch = malloc(sizeof(DataEntry_t) * RESUME_BATCH_SIZE);
    if (batch == NULL) return;
    int total_sent = 0;

    while (1) {
        int count = RESUME_BATCH_SIZE;
        StorageQueryUnsynced(batch, &count);
        if (count == 0) break;

        int ids[RESUME_BATCH_SIZE];
        int sent_ok = 0;
        for (int i = 0; i < count; i++) {
            if (mqtt_send_all(&batch[i].data) != 0) {
                LogWrite(LOG_LEVEL_ERROR, "MQTT: resume send failed, will retry later");
                break;
            }
            ids[sent_ok++] = batch[i].id;
        }

        if (sent_ok > 0) {
            StorageMarkSyncedBatch(ids, sent_ok);
            total_sent += sent_ok;
        }

        if (sent_ok < count) break;
    }

    if (total_sent > 0) {
        char log_buf[64] = {0};
        snprintf(log_buf, sizeof(log_buf), "MQTT: resumed %d unsynced records", total_sent);
        LogWrite(LOG_LEVEL_INFO, log_buf);
    }
    free(batch);
}

static void *pthread_mqtt(void *arg)
{
    Data_t data = {0};
    int offline_mode = 0;
    int recv_count = 0;
    int reconnect_delay = 2;

    if (mqtt_init() != 0) {
        LogWrite(LOG_LEVEL_ERROR, "MQTT: init failed, entering offline mode");
        offline_mode = 1;
    }

    while(1)
    {
        MailBoxRecvMsg(&data);
        recv_count++;

        char dbg[128] = {0};
        snprintf(dbg, sizeof(dbg), "MQTT: recv #%d, connected=%d, Tem=%.1f Gas=%.1f Acc=%.3f",
                 recv_count, mqtt_is_connected(), data.Tem, data.Gas, data.Acc_xyz);
        LogWrite(LOG_LEVEL_INFO, dbg);

        if (!mqtt_is_connected()) {
            // 重连在主循环内完成，避免跨线程操作 MQTTClient
            offline_mode = 1;
            char log_buf[128] = {0};
            snprintf(log_buf, sizeof(log_buf), "MQTT reconnecting... (delay=%ds)", reconnect_delay);
            LogWrite(LOG_LEVEL_INFO, log_buf);
            sleep(reconnect_delay);

            if (mqtt_reconnect() == 0) {
                reconnect_delay = 2;
                offline_mode = 0;
                LogWrite(LOG_LEVEL_INFO, "MQTT: back online, resuming...");
                resume_unsynced_data();
            } else {
                reconnect_delay *= 2;
                if (reconnect_delay > 60) reconnect_delay = 60;
                continue;  // 重连失败，跳过本次发送
            }
        }

        if (mqtt_is_connected()) {
            if (mqtt_send_all(&data) != 0) {
                LogWrite(LOG_LEVEL_WARN, "MQTT: send failed, entering offline mode");
                offline_mode = 1;
            }
        }
    }

    mqtt_deinit();
    return 0;
}

// ============ LVGL 接收线程 ============

static void *pthread_lvgl_recv(void *arg)
{
    Data_t data = {0};

    while(1)
    {
        MailBoxRecvMsg(&data);
        LvglUiUpdateData(&data);
    }

    return 0;
}

// ============ 设备管理接口（供 UI 调用） ============

int Rs485AddDevice(int dev_id)
{
    if (dev_id < 1 || dev_id > 247) return -1;

    pthread_mutex_lock(&g_rs485_lock);
    for (int i = 0; i < g_rs485_count; i++) {
        if (g_rs485_ids[i] == dev_id) {
            pthread_mutex_unlock(&g_rs485_lock);
            return -1;
        }
    }
    if (g_rs485_count >= RS485_MAX_DEV) {
        pthread_mutex_unlock(&g_rs485_lock);
        return -1;
    }
    g_rs485_ids[g_rs485_count++] = dev_id;
    pthread_mutex_unlock(&g_rs485_lock);

    // 在 RS485 缓冲中预分配条目，确保上传始终包含该设备（初值 0）
    pthread_mutex_lock(&g_rs485_buf_lock);
    if (g_rs485_buf_count < RS485_MAX_DEV) {
        g_rs485_buf[g_rs485_buf_count].id = dev_id;
        g_rs485_buf[g_rs485_buf_count].Volt = 0.0;
        g_rs485_buf[g_rs485_buf_count].Elec = 0.0;
        g_rs485_buf_count++;
    }
    pthread_mutex_unlock(&g_rs485_buf_lock);

    char log_buf[64] = {0};
    snprintf(log_buf, sizeof(log_buf), "RS485: added device %d", dev_id);
    LogWrite(LOG_LEVEL_INFO, log_buf);
    return 0;
}

int Rs485RemoveDevice(int dev_id)
{
    pthread_mutex_lock(&g_rs485_lock);
    int removed = 0;
    for (int i = 0; i < g_rs485_count; i++) {
        if (g_rs485_ids[i] == dev_id) {
            g_rs485_ids[i] = g_rs485_ids[g_rs485_count - 1];
            g_rs485_count--;
            removed = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_rs485_lock);

    if (removed) {
        pthread_mutex_lock(&g_rs485_buf_lock);
        for (int i = 0; i < g_rs485_buf_count; i++) {
            if (g_rs485_buf[i].id == dev_id) {
                g_rs485_buf[i] = g_rs485_buf[g_rs485_buf_count - 1];
                g_rs485_buf_count--;
                break;
            }
        }
        pthread_mutex_unlock(&g_rs485_buf_lock);
    }

    return removed ? 0 : -1;
}

int Rs485GetDevices(int *ids, int *count)
{
    pthread_mutex_lock(&g_rs485_lock);
    *count = g_rs485_count;
    memcpy(ids, g_rs485_ids, sizeof(int) * g_rs485_count);
    pthread_mutex_unlock(&g_rs485_lock);
    return 0;
}

int CanAddDevice(int can_id)
{
    pthread_mutex_lock(&g_can_lock);
    for (int i = 0; i < g_can_count; i++) {
        if (g_can_ids[i] == can_id) {
            pthread_mutex_unlock(&g_can_lock);
            return -1;
        }
    }
    if (g_can_count >= CAN_MAX_DEV) {
        pthread_mutex_unlock(&g_can_lock);
        return -1;
    }
    g_can_ids[g_can_count++] = can_id;
    pthread_mutex_unlock(&g_can_lock);

    // 在 CAN 缓冲中预分配条目，确保 MQTT 上传始终包含该设备（初值 0）
    pthread_mutex_lock(&g_can_buf_lock);
    if (g_can_buf_count < CAN_MAX_DEV) {
        g_can_buf[g_can_buf_count].id = can_id;
        g_can_buf[g_can_buf_count].Speed = 0.0;
        g_can_buf[g_can_buf_count].Flow = 0.0;
        g_can_buf_count++;
    }
    pthread_mutex_unlock(&g_can_buf_lock);

    char log_buf[64] = {0};
    snprintf(log_buf, sizeof(log_buf), "CAN: added device ID 0x%X", can_id);
    LogWrite(LOG_LEVEL_INFO, log_buf);
    return 0;
}

int CanRemoveDevice(int can_id)
{
    // 从注册列表移除
    pthread_mutex_lock(&g_can_lock);
    int removed = 0;
    for (int i = 0; i < g_can_count; i++) {
        if (g_can_ids[i] == can_id) {
            g_can_ids[i] = g_can_ids[g_can_count - 1];
            g_can_count--;
            removed = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_can_lock);

    // 同步从 CAN 缓冲中移除
    if (removed) {
        pthread_mutex_lock(&g_can_buf_lock);
        for (int i = 0; i < g_can_buf_count; i++) {
            if (g_can_buf[i].id == can_id) {
                g_can_buf[i] = g_can_buf[g_can_buf_count - 1];
                g_can_buf_count--;
                break;
            }
        }
        pthread_mutex_unlock(&g_can_buf_lock);
    }

    return removed ? 0 : -1;
}

int CanGetDevices(int *ids, int *count)
{
    pthread_mutex_lock(&g_can_lock);
    *count = g_can_count;
    memcpy(ids, g_can_ids, sizeof(int) * g_can_count);
    pthread_mutex_unlock(&g_can_lock);
    return 0;
}

// ============ main ============

int main(void)
{
    char ch = 0;

    LogInit("log");
    SetCurLogLevel(LOG_LEVEL_INFO);

    // 必须在 LvglUiInit/AlarmMgrInit 之前初始化数据库，
    // 否则 AlarmMgrInit->StorageLoadThreshold 会因 g_db_handle==NULL 而加载默认值
    StorageInit("info.db");

    LvglUiInit();
    MailBoxInit();

    RegisterMailBoxTask("collect", pthread_collect);
    RegisterMailBoxTaskEx("storage", pthread_storage, 50);
    RegisterMailBoxTaskEx("mqtt", pthread_mqtt, 200);
    RegisterMailBoxTaskEx("lvgl", pthread_lvgl_recv, 5);

    // RS485 和 CAN 线程独立启动（只更新全局缓冲，不走邮箱）
    pthread_t rs485_tid, can_tid;
    pthread_create(&rs485_tid, NULL, pthread_rs485, NULL);
    pthread_create(&can_tid, NULL, pthread_can, NULL);

    pthread_create(&lvgl_tid, NULL, LvglUiThread, NULL);

    while(1)
    {
        ch = getchar();
        getchar();

        if(ch == 'q')
        {
            break;
        }
    }

    LvglUiDeinit();
    pthread_join(lvgl_tid, NULL);

    MailBoxWaitAllTask();
    LogDeInit();

    return 0;
}
