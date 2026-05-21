#ifndef __STORAGE_H__
#define __STORAGE_H__

#include "mailbox.h"
#include "log.h"
#include <sqlite3.h>
#include <pthread.h>

#define STORAGE_BATCH_SIZE  10
#define STORAGE_BATCH_TIMEOUT_SEC 30

// 带时间戳和数据库 ID 的数据条目（用于断网续传查询）
typedef struct {
    int id;
    char timestamp[24];
    Data_t data;
} DataEntry_t;

extern sqlite3 *g_db_handle;
extern pthread_mutex_t g_storage_lock;

int StorageInit(const char *db_path);
int StorageDeInit(void);
int StorageInsertData(const Data_t *p_data);
int StorageQueryData(const char *start_time, const char *end_time, Data_t *p_result, int *p_count);
int StorageQueryDataEntries(const char *start_time, const char *end_time, DataEntry_t *p_result, int *p_count);
int StorageCleanOldData(int keep_days);

// 批量写入（事务）
int StorageBatchInsert(const Data_t *p_data);
int StorageFlushBatch(void);
int StorageCheckpoint(void);

// 断网续传
int StorageQueryUnsynced(DataEntry_t *p_result, int *p_count);
int StorageMarkSyncedAll(void);
int StorageMarkSyncedBatch(const int *ids, int count);

// 告警持久化
int StorageInsertAlarm(const char *timestamp, const char *source, const char *type,
                       double value, double threshold);
int StorageInitAlarmTable(void);

// 阈值持久化（16 字段：上限 + 下限）
int StorageLoadThreshold(double *tem_max, double *tem_min,
                         double *hum_max, double *hum_min,
                         double *gas_max, double *gas_min,
                         double *acc_max, double *acc_min,
                         double *volt_max, double *volt_min,
                         double *elec_max, double *elec_min,
                         double *can_speed_max, double *can_speed_min,
                         double *can_flow_max, double *can_flow_min);
int StorageSaveThreshold(double tem_max, double tem_min,
                         double hum_max, double hum_min,
                         double gas_max, double gas_min,
                         double acc_max, double acc_min,
                         double volt_max, double volt_min,
                         double elec_max, double elec_min,
                         double can_speed_max, double can_speed_min,
                         double can_flow_max, double can_flow_min);
int StorageInitThresholdTable(void);

// 用户凭据持久化（加密存储）
int StorageInitUserTable(void);
int StorageSaveCredential(const char *username, const char *password);
int StorageLoadCredential(const char *username, char *password, int max_len);

#endif
