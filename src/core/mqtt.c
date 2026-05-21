#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include "mqtt.h"
#include "log.h"

static char topic[2][200] = {{0}};
static MQTTClient client;
static int id = 10000;
static volatile MQTTClient_deliveryToken deliveredtoken;
static volatile int g_mqtt_connected = 0;
static int g_reconnect_delay = 2;  // 重连退避，指数增长

void pack_topic(char *dev_name, char *pro_id)
{
    sprintf(topic[0], "$sys/%s/%s/thing/property/post/reply", pro_id, dev_name);
    sprintf(topic[1], "$sys/%s/%s/thing/property/post", pro_id, dev_name);
}

void delivered(void *context, MQTTClient_deliveryToken dt)
{
    deliveredtoken = dt;
}

int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
    char log_buff[256] = {0};
    snprintf(log_buff, sizeof(log_buff), "Message arrived, topic: %s", topicName);
    LogWrite(LOG_LEVEL_INFO, log_buff);

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void connlost(void *context, char *cause)
{
    char log_buff[256] = {0};
    snprintf(log_buff, sizeof(log_buff), "Connection lost: %s", cause);
    LogWrite(LOG_LEVEL_ERROR, log_buff);
    g_mqtt_connected = 0;
}

int mqtt_is_connected(void)
{
    return g_mqtt_connected;
}

int mqtt_init(void)
{
    pack_topic(DEV_NAME, PRODUCT_ID);

    int rc = MQTTClient_create(&client, NEW_ADDRESS, CLIENTID,
                               MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (MQTTCLIENT_SUCCESS != rc)
    {
        LogWrite(LOG_LEVEL_ERROR, "create mqtt client failure");
        return -1;
    }

    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = PRODUCT_ID;
    conn_opts.password = PASSWD;
    conn_opts.connectTimeout = 10;
    conn_opts.retryInterval = 5;

    rc = MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);
    if (MQTTCLIENT_SUCCESS != rc)
    {
        char log_buff[128] = {0};
        snprintf(log_buff, sizeof(log_buff), "Failed to set callbacks, return code %d", rc);
        LogWrite(LOG_LEVEL_ERROR, log_buff);
        return -1;
    }

    rc = MQTTClient_connect(client, &conn_opts);
    if (MQTTCLIENT_SUCCESS != rc)
    {
        char log_buff[128] = {0};
        snprintf(log_buff, sizeof(log_buff), "Failed to connect to mqtt server, return code %d", rc);
        LogWrite(LOG_LEVEL_ERROR, log_buff);
        g_mqtt_connected = 0;
        return -1;
    }

    g_mqtt_connected = 1;
    g_reconnect_delay = 2;
    LogWrite(LOG_LEVEL_INFO, "mqtt client init success");

    // 订阅属性设置回复主题，用于接收云端响应
    rc = MQTTClient_subscribe(client, topic[0], QOS);
    if (MQTTCLIENT_SUCCESS != rc) {
        char log_buff[128] = {0};
        snprintf(log_buff, sizeof(log_buff), "mqtt subscribe reply topic failed, rc=%d", rc);
        LogWrite(LOG_LEVEL_WARN, log_buff);
    } else {
        LogWrite(LOG_LEVEL_INFO, "mqtt subscribed to reply topic");
    }

    return 0;
}

int mqtt_reconnect(void)
{
    if (g_mqtt_connected) return 0;

    char log_buf[128] = {0};
    snprintf(log_buf, sizeof(log_buf), "MQTT reconnecting... (delay=%ds)", g_reconnect_delay);
    LogWrite(LOG_LEVEL_INFO, log_buf);

    sleep(g_reconnect_delay);

    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = PRODUCT_ID;
    conn_opts.password = PASSWD;
    conn_opts.connectTimeout = 10;

    int rc = MQTTClient_connect(client, &conn_opts);
    if (MQTTCLIENT_SUCCESS != rc)
    {
        // 指数退避，最大 60 秒
        g_reconnect_delay *= 2;
        if (g_reconnect_delay > 60) g_reconnect_delay = 60;
        return -1;
    }

    g_mqtt_connected = 1;
    g_reconnect_delay = 2;
    LogWrite(LOG_LEVEL_INFO, "MQTT reconnected success");

    // 重新订阅
    MQTTClient_subscribe(client, topic[0], QOS);
    return 0;
}

int mqtt_send(char *key, double value)
{
    if (!g_mqtt_connected) return -1;

    MQTTClient_deliveryToken deliveryToken;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    static char message[1024];
    memset(message, 0, sizeof(message));
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    pubmsg.payload = message;

    snprintf(message, sizeof(message),
             "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":{\"%s\":{\"value\":%.2lf}}}",
             id++, key, value);
    pubmsg.payloadlen = strlen(message);

    int rc = MQTTClient_publishMessage(client, topic[1], &pubmsg, &deliveryToken);
    if (MQTTCLIENT_SUCCESS != rc)
    {
        char log_buff[128] = {0};
        snprintf(log_buff, sizeof(log_buff), "mqtt publish failure, rc=%d", rc);
        LogWrite(LOG_LEVEL_ERROR, log_buff);
        g_mqtt_connected = 0;
        return -1;
    }

    MQTTClient_waitForCompletion(client, deliveryToken, TIMEOUT);
    return 0;
}

// 一次性上报所有参数（合并发送，减少网络开销）
int mqtt_send_all(const Data_t *data)
{
    if (data == NULL) return -1;
    if (!g_mqtt_connected) {
        LogWrite(LOG_LEVEL_WARN, "mqtt_send_all: not connected, skip");
        return -1;
    }

    MQTTClient_deliveryToken deliveryToken;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    static char message[8192];
    int offset = 0;
    memset(message, 0, sizeof(message));

    pubmsg.qos = QOS;
    pubmsg.retained = 0;

    // 组装基础传感器数据
    offset += snprintf(message + offset, sizeof(message) - offset,
        "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":{"
        "\"Tem\":{\"value\":%.2lf},"
        "\"Hum\":{\"value\":%.2lf},"
        "\"Gas\":{\"value\":%.2lf},"
        "\"Acc_xyz\":{\"value\":%.2lf}",
        id++, data->Tem, data->Hum, data->Gas, data->Acc_xyz);

    // RS485 设备数组
    if (data->rs485_count > 0) {
        offset += snprintf(message + offset, sizeof(message) - offset,
            ",\"rs485_device\":{\"value\":[");
        for (int i = 0; i < data->rs485_count; i++) {
            if (i > 0) {
                offset += snprintf(message + offset, sizeof(message) - offset, ",");
            }
            offset += snprintf(message + offset, sizeof(message) - offset,
                "{\"id\":%d,\"Volt\":%.2lf,\"Elec\":%.2lf}",
                data->rs485_dev[i].id, data->rs485_dev[i].Volt, data->rs485_dev[i].Elec);
        }
        offset += snprintf(message + offset, sizeof(message) - offset, "]}");
    }

    // CAN 设备数组
    if (data->can_count > 0) {
        offset += snprintf(message + offset, sizeof(message) - offset,
            ",\"can_device\":{\"value\":[");
        for (int i = 0; i < data->can_count; i++) {
            if (i > 0) {
                offset += snprintf(message + offset, sizeof(message) - offset, ",");
            }
            offset += snprintf(message + offset, sizeof(message) - offset,
                "{\"id\":%d,\"Speed\":%.2lf,\"Flow\":%.2lf}",
                data->can_dev[i].id, data->can_dev[i].Speed, data->can_dev[i].Flow);
        }
        offset += snprintf(message + offset, sizeof(message) - offset, "]}");
    }

    offset += snprintf(message + offset, sizeof(message) - offset, "}}");
    pubmsg.payload = message;
    pubmsg.payloadlen = strlen(message);

    int rc = MQTTClient_publishMessage(client, topic[1], &pubmsg, &deliveryToken);
    if (MQTTCLIENT_SUCCESS != rc)
    {
        char log_buff[128] = {0};
        snprintf(log_buff, sizeof(log_buff), "mqtt_send_all: publish failure, rc=%d", rc);
        LogWrite(LOG_LEVEL_ERROR, log_buff);
        g_mqtt_connected = 0;
        return -1;
    }

    rc = MQTTClient_waitForCompletion(client, deliveryToken, TIMEOUT);
    if (rc != MQTTCLIENT_SUCCESS) {
        char log_buff[128] = {0};
        snprintf(log_buff, sizeof(log_buff), "mqtt_send_all: waitForCompletion failed, rc=%d", rc);
        LogWrite(LOG_LEVEL_ERROR, log_buff);
        g_mqtt_connected = 0;
        return -1;
    }

    char log_buf[128] = {0};
    snprintf(log_buf, sizeof(log_buf), "mqtt_send_all: published OK, payload %d bytes, Tem=%.1f Acc=%.3f",
             pubmsg.payloadlen, data->Tem, data->Acc_xyz);
    LogWrite(LOG_LEVEL_INFO, log_buf);
    return 0;
}

void mqtt_deinit(void)
{
    if (g_mqtt_connected) {
        MQTTClient_disconnect(client, 10000);
    }
    MQTTClient_destroy(&client);
    g_mqtt_connected = 0;
    LogWrite(LOG_LEVEL_INFO, "mqtt client deinit success");
}
