#ifndef __MAILBOX_H__
#define __MAILBOX_H__

#include <pthread.h>

#define RS485_MAX_DEV       50
#define CAN_MAX_DEV         50
#define MAILBOX_DEFAULT_MAX 200

typedef struct {
    int id;
    double Volt;
    double Elec;
} Rs485Device_t;

typedef struct {
    int id;
    double Speed;
    double Flow;
} CanDevice_t;

typedef struct Data
{
    // 本地传感器
    double Tem;
    double Hum;
    double Gas;
    double Acc_xyz;

    // RS485 设备
    int rs485_count;
    Rs485Device_t rs485_dev[RS485_MAX_DEV];

    // CAN 设备
    int can_count;
    CanDevice_t can_dev[CAN_MAX_DEV];
} Data_t;

typedef struct ListNode
{
    Data_t Tmpdata;
    struct ListNode *pNextListNode;
} ListNode_t;

typedef struct PthreadNode
{
    pthread_t Tid;
    char pThreadName[32];
    void *(* pThreadFun)(void *);
    struct PthreadNode *pNextPthreadNode;
    struct ListNode *pListHead;
    pthread_mutex_t ListLock;
    pthread_cond_t ListCond;
    int max_len;        // 消息队列最大长度
    int cur_len;        // 当前消息数量
} PthreadNode_t;

extern int MailBoxInit(void);
extern int RegisterMailBoxTask(char *pThreadName, void *(*pThreadFun)(void *arg));
extern int RegisterMailBoxTaskEx(char *pThreadName, void *(*pThreadFun)(void *arg), int max_msg);
extern int MailBoxWaitAllTask(void);
extern int MailBoxSendMsg(char *pRecvThreadName, Data_t TmpData);
extern int MailBoxRecvMsg(Data_t *pTmpData);

#endif
