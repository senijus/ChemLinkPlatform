#include "mailbox.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

pthread_mutex_t PthreadLock;
PthreadNode_t *PthreadHead;

int MailBoxInit(void)
{
    int ret = 0;

    if(PthreadHead != NULL)
    {
        return 0;
    }

    PthreadHead = malloc(sizeof(PthreadNode_t));
    if(NULL == PthreadHead)
    {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "fail to malloc PthreadHead: %s", strerror(errno));
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        return -1;
    }

    ret = pthread_mutex_init(&PthreadLock, NULL);
    if(ret != 0)
    {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "fail to pthread_mutex_init pthreadlock: %s", strerror(ret));
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        return -1;
    }

    ret = pthread_mutex_init(&(PthreadHead->ListLock), NULL);
    if(ret != 0)
    {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "fail to pthread_mutex_init listlock: %s", strerror(ret));
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        return -1;
    }

    ret = pthread_cond_init(&(PthreadHead->ListCond), NULL);
    if(ret != 0)
    {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "fail to pthread_cond_init listcond: %s", strerror(ret));
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        pthread_mutex_destroy(&(PthreadHead->ListLock));
        free(PthreadHead);
        return -1;
    }

    PthreadHead->pNextPthreadNode = NULL;
    PthreadHead->pListHead = NULL;
    memset(PthreadHead->pThreadName, 0, sizeof(PthreadHead->pThreadName));
    PthreadHead->Tid = 0;
    PthreadHead->pThreadFun = NULL;
    PthreadHead->max_len = MAILBOX_DEFAULT_MAX;
    PthreadHead->cur_len = 0;

    return 0;
}

static int _RegisterMailBoxTask(char *pThreadName, void *(*pThreadFun)(void *arg), int max_msg)
{
    PthreadNode_t *pnewthreadnode = NULL;
    int ret = 0;

    pnewthreadnode = malloc(sizeof(PthreadNode_t));
    if(NULL == pnewthreadnode)
    {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "fail to malloc pnewthreadnode: %s", strerror(errno));
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        return -1;
    }

    pnewthreadnode->pListHead = malloc(sizeof(ListNode_t));
    if(NULL == pnewthreadnode->pListHead)
    {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "fail to malloc plisthead: %s", strerror(errno));
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        free(pnewthreadnode);
        return -1;
    }

    ret = pthread_mutex_init(&(pnewthreadnode->ListLock), NULL);
    if(ret != 0)
    {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "fail to pthread_mutex_init listlock: %s", strerror(ret));
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        free(pnewthreadnode->pListHead);
        free(pnewthreadnode);
        return -1;
    }

    ret = pthread_cond_init(&(pnewthreadnode->ListCond), NULL);
    if(ret != 0)
    {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "fail to pthread_cond_init listcond: %s", strerror(ret));
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        pthread_mutex_destroy(&(pnewthreadnode->ListLock));
        free(pnewthreadnode->pListHead);
        free(pnewthreadnode);
        return -1;
    }

    strncpy(pnewthreadnode->pThreadName, pThreadName, sizeof(pnewthreadnode->pThreadName) - 1);
    pnewthreadnode->pThreadName[sizeof(pnewthreadnode->pThreadName) - 1] = '\0';
    pnewthreadnode->pThreadFun = pThreadFun;
    pnewthreadnode->pListHead->pNextListNode = NULL;
    pnewthreadnode->pNextPthreadNode = NULL;
    pnewthreadnode->Tid = 0;
    pnewthreadnode->max_len = max_msg;
    pnewthreadnode->cur_len = 0;

    pthread_mutex_lock(&PthreadLock);
    pnewthreadnode->pNextPthreadNode = PthreadHead->pNextPthreadNode;
    PthreadHead->pNextPthreadNode = pnewthreadnode;
    pthread_mutex_unlock(&PthreadLock);

    ret = pthread_create(&(pnewthreadnode->Tid), NULL, pnewthreadnode->pThreadFun, NULL);
    if(ret != 0)
    {
        pthread_mutex_lock(&PthreadLock);
        PthreadNode_t *pPrevNode = PthreadHead;
        while(pPrevNode != NULL && pPrevNode->pNextPthreadNode != pnewthreadnode)
        {
            pPrevNode = pPrevNode->pNextPthreadNode;
        }
        if(pPrevNode != NULL)
        {
            pPrevNode->pNextPthreadNode = pnewthreadnode->pNextPthreadNode;
        }
        pthread_mutex_unlock(&PthreadLock);

        pthread_cond_destroy(&(pnewthreadnode->ListCond));
        pthread_mutex_destroy(&(pnewthreadnode->ListLock));
        free(pnewthreadnode->pListHead);
        free(pnewthreadnode);

        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "fail to pthread_create: %s", strerror(ret));
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        return -1;
    }

    return 0;
}

int RegisterMailBoxTask(char *pThreadName, void *(*pThreadFun)(void *arg))
{
    return _RegisterMailBoxTask(pThreadName, pThreadFun, MAILBOX_DEFAULT_MAX);
}

int RegisterMailBoxTaskEx(char *pThreadName, void *(*pThreadFun)(void *arg), int max_msg)
{
    return _RegisterMailBoxTask(pThreadName, pThreadFun, max_msg);
}

int MailBoxWaitAllTask(void)
{
    PthreadNode_t *ptmpPthreadNode = NULL;
    PthreadNode_t *pfreePthreadNode = NULL;
    ListNode_t *ptmpListNode = NULL;
    ListNode_t *pfreeListNode = NULL;

    ptmpPthreadNode = PthreadHead->pNextPthreadNode;
    pfreePthreadNode = PthreadHead->pNextPthreadNode;

    while(ptmpPthreadNode != NULL)
    {
        pthread_join(ptmpPthreadNode->Tid, NULL);

        pthread_cond_destroy(&(ptmpPthreadNode->ListCond));
        pthread_mutex_destroy(&(ptmpPthreadNode->ListLock));

        ptmpListNode = ptmpPthreadNode->pListHead;
        pfreeListNode = ptmpPthreadNode->pListHead;

        while(ptmpListNode != NULL)
        {
            ptmpListNode = ptmpListNode->pNextListNode;
            free(pfreeListNode);
            pfreeListNode = ptmpListNode;
        }

        ptmpPthreadNode = ptmpPthreadNode->pNextPthreadNode;
        free(pfreePthreadNode);
        pfreePthreadNode = ptmpPthreadNode;
    }

    pthread_mutex_destroy(&PthreadLock);
    pthread_mutex_destroy(&(PthreadHead->ListLock));
    pthread_cond_destroy(&(PthreadHead->ListCond));
    free(PthreadHead);

    PthreadHead = NULL;

    return 0;
}

int MailBoxSendMsg(char *pRecvThreadName, Data_t TmpData)
{
    PthreadNode_t *ptmpPthreadNode = NULL;
    ListNode_t *newListNode = NULL;
    ListNode_t *tmpListNode = NULL;

    ptmpPthreadNode = PthreadHead->pNextPthreadNode;

    pthread_mutex_lock(&PthreadLock);
    while(ptmpPthreadNode != NULL)
    {
        if(0 == strcmp(ptmpPthreadNode->pThreadName, pRecvThreadName))
        {
            break;
        }
        ptmpPthreadNode = ptmpPthreadNode->pNextPthreadNode;
    }
    pthread_mutex_unlock(&PthreadLock);

    if(NULL == ptmpPthreadNode)
    {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "fail to find RecvThreadName: %s", pRecvThreadName);
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        return -1;
    }

    newListNode = malloc(sizeof(ListNode_t));
    if(NULL == newListNode)
    {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "fail to malloc newlistnode: %s", strerror(errno));
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        return -1;
    }
    newListNode->pNextListNode = NULL;
    newListNode->Tmpdata = TmpData;

    pthread_mutex_lock(&(ptmpPthreadNode->ListLock));

    // 有界队列：满时批量丢弃到 80% 水位（减少频繁 drop 开销）
    if(ptmpPthreadNode->cur_len >= ptmpPthreadNode->max_len)
    {
        int target = ptmpPthreadNode->max_len * 4 / 5;
        int dropped = 0;
        while(ptmpPthreadNode->cur_len > target)
        {
            ListNode_t *dropNode = ptmpPthreadNode->pListHead->pNextListNode;
            if(dropNode == NULL) break;
            ptmpPthreadNode->pListHead->pNextListNode = dropNode->pNextListNode;
            free(dropNode);
            ptmpPthreadNode->cur_len--;
            dropped++;
        }

        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf),
                 "mailbox[%s] overflow, dropped %d msgs, now %d/%d",
                 ptmpPthreadNode->pThreadName, dropped,
                 ptmpPthreadNode->cur_len, ptmpPthreadNode->max_len);
        LogWrite(LOG_LEVEL_WARN, log_buf);
    }

    // 插入队尾
    tmpListNode = ptmpPthreadNode->pListHead;
    while(tmpListNode->pNextListNode != NULL)
    {
        tmpListNode = tmpListNode->pNextListNode;
    }
    tmpListNode->pNextListNode = newListNode;
    ptmpPthreadNode->cur_len++;

    pthread_cond_signal(&(ptmpPthreadNode->ListCond));
    pthread_mutex_unlock(&(ptmpPthreadNode->ListLock));

    return 0;
}

int MailBoxRecvMsg(Data_t *pTmpData)
{
    pthread_t tid;
    PthreadNode_t *ptmpPthreadNode = NULL;
    ListNode_t *freeListNode = NULL;

    tid = pthread_self();
    ptmpPthreadNode = PthreadHead->pNextPthreadNode;

    while(ptmpPthreadNode != NULL)
    {
        if(ptmpPthreadNode->Tid == tid)
        {
            break;
        }
        ptmpPthreadNode = ptmpPthreadNode->pNextPthreadNode;
    }

    if(NULL == ptmpPthreadNode)
    {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "fail to find self thread node, tid=%lu", (unsigned long)tid);
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        return -1;
    }

    pthread_mutex_lock(&(ptmpPthreadNode->ListLock));

    while(ptmpPthreadNode->pListHead->pNextListNode == NULL)
    {
        pthread_cond_wait(&(ptmpPthreadNode->ListCond), &(ptmpPthreadNode->ListLock));
    }

    freeListNode = ptmpPthreadNode->pListHead->pNextListNode;
    *pTmpData = freeListNode->Tmpdata;
    ptmpPthreadNode->pListHead->pNextListNode = freeListNode->pNextListNode;
    free(freeListNode);
    freeListNode = NULL;
    ptmpPthreadNode->cur_len--;

    pthread_mutex_unlock(&(ptmpPthreadNode->ListLock));

    return 0;
}
