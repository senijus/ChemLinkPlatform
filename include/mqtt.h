#ifndef __MQTT_H__
#define __MQTT_H__

#include <MQTTAsync.h>
#include <MQTTClient.h>

#define NEW_ADDRESS     "tcp://183.230.40.96:1883"
#define DEV_NAME        "device_01"
#define CLIENTID        DEV_NAME
#define PRODUCT_ID      "4O48CuvH1O"
#define PASSWD          "version=2018-10-31&res=products%2F4O48CuvH1O%2Fdevices%2Fdevice_01&et=1837255523&method=md5&sign=WwnLyAFCxHPEpjYxoZ42UQ%3D%3D"
#define QOS             0
#define TIMEOUT         5000L

#include "mailbox.h"

extern void pack_topic(char *dev_name, char *pro_id);
extern void delivered(void *context, MQTTClient_deliveryToken dt);
extern int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message);
extern void connlost(void *context, char *cause);

extern int mqtt_init(void);
extern int mqtt_send(char *key, double value);
extern int mqtt_send_all(const Data_t *data);
extern void mqtt_deinit(void);
extern int mqtt_is_connected(void);
extern int mqtt_reconnect(void);

#endif
