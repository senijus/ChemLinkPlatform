#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

sqlite3 *g_db_handle = NULL;
pthread_mutex_t g_storage_lock;

// 批量写入缓冲区
static Data_t g_batch_buf[STORAGE_BATCH_SIZE];
static int g_batch_count = 0;
static time_t g_batch_last_flush = 0;

typedef struct {
    Data_t *p_data_arr;
    int *p_count;
    int max_count;
} QueryCallbackParam_t;

typedef struct {
    DataEntry_t *p_entry_arr;
    int *p_count;
    int max_count;
} UnsyncedCallbackParam_t;

// 序列化 RS485 设备数组为 JSON 字符串
static void serialize_rs485_json(const Data_t *p_data, char *buf, int buf_size)
{
    int offset = 0;
    offset += snprintf(buf + offset, buf_size - offset, "[");
    for (int i = 0; i < p_data->rs485_count; i++) {
        if (i > 0) offset += snprintf(buf + offset, buf_size - offset, ",");
        offset += snprintf(buf + offset, buf_size - offset,
            "{\"id\":%d,\"Volt\":%.2f,\"Elec\":%.2f}",
            p_data->rs485_dev[i].id, p_data->rs485_dev[i].Volt,
            p_data->rs485_dev[i].Elec);
    }
    snprintf(buf + offset, buf_size - offset, "]");
}

// 序列化 CAN 设备数组为 JSON 字符串
static void serialize_can_json(const Data_t *p_data, char *buf, int buf_size)
{
    int offset = 0;
    offset += snprintf(buf + offset, buf_size - offset, "[");
    for (int i = 0; i < p_data->can_count; i++) {
        if (i > 0) offset += snprintf(buf + offset, buf_size - offset, ",");
        offset += snprintf(buf + offset, buf_size - offset,
            "{\"id\":%d,\"Speed\":%.2f,\"Flow\":%.2f}",
            p_data->can_dev[i].id, p_data->can_dev[i].Speed,
            p_data->can_dev[i].Flow);
    }
    snprintf(buf + offset, buf_size - offset, "]");
}

static int query_data_callback(void *param, int col_num, char **col_val, char **col_name) {
    QueryCallbackParam_t *p_cb_param = (QueryCallbackParam_t *)param;
    if (*(p_cb_param->p_count) >= p_cb_param->max_count) {
        return 1;
    }
    Data_t *p_data = &(p_cb_param->p_data_arr[*(p_cb_param->p_count)]);
    // col_val: [0]=id, [1]=timestamp, [2]=temperature, [3]=humidity, [4]=gas, [5]=acc_xyz, [6]=rs485_json, [7]=can_json, [8]=synced
    p_data->Tem = atof(col_val[2]);
    p_data->Hum = atof(col_val[3]);
    p_data->Gas = atof(col_val[4]);
    p_data->Acc_xyz = atof(col_val[5]);
    p_data->rs485_count = 0;
    p_data->can_count = 0;
    (*p_cb_param->p_count)++;
    return 0;
}

// 带时间戳的查询回调
typedef struct {
    DataEntry_t *p_entry_arr;
    int *p_count;
    int max_count;
} QueryEntryCallbackParam_t;

static int query_entry_callback(void *param, int col_num, char **col_val, char **col_name) {
    QueryEntryCallbackParam_t *p_cb_param = (QueryEntryCallbackParam_t *)param;
    if (*(p_cb_param->p_count) >= p_cb_param->max_count) {
        return 1;
    }
    DataEntry_t *p_entry = &(p_cb_param->p_entry_arr[*(p_cb_param->p_count)]);
    p_entry->id = atoi(col_val[0]);
    strncpy(p_entry->timestamp, col_val[1] ? col_val[1] : "", sizeof(p_entry->timestamp) - 1);
    p_entry->timestamp[sizeof(p_entry->timestamp) - 1] = '\0';
    p_entry->data.Tem = atof(col_val[2]);
    p_entry->data.Hum = atof(col_val[3]);
    p_entry->data.Gas = atof(col_val[4]);
    p_entry->data.Acc_xyz = atof(col_val[5]);
    p_entry->data.rs485_count = 0;
    p_entry->data.can_count = 0;
    (*p_cb_param->p_count)++;
    return 0;
}

static int unsynced_callback(void *param, int col_num, char **col_val, char **col_name) {
    UnsyncedCallbackParam_t *p_cb_param = (UnsyncedCallbackParam_t *)param;
    if (*(p_cb_param->p_count) >= p_cb_param->max_count) {
        return 1;
    }
    DataEntry_t *p_entry = &(p_cb_param->p_entry_arr[*(p_cb_param->p_count)]);
    p_entry->id = atoi(col_val[0]);
    strncpy(p_entry->timestamp, col_val[1] ? col_val[1] : "", sizeof(p_entry->timestamp) - 1);
    p_entry->data.Tem = atof(col_val[2]);
    p_entry->data.Hum = atof(col_val[3]);
    p_entry->data.Gas = atof(col_val[4]);
    p_entry->data.Acc_xyz = atof(col_val[5]);
    p_entry->data.rs485_count = 0;
    p_entry->data.can_count = 0;
    (*p_cb_param->p_count)++;
    return 0;
}

int StorageInit(const char *db_path) {
    if (g_db_handle != NULL) {
        LogWrite(LOG_LEVEL_INFO, "StorageInit: already initialized");
        return 0;
    }

    int ret = 0;
    char *err_msg = NULL;

    ret = pthread_mutex_init(&g_storage_lock, NULL);
    if (ret != 0) {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "StorageInit: mutex init fail: %s", strerror(ret));
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        return -1;
    }

    ret = sqlite3_open(db_path, &g_db_handle);
    if (ret != SQLITE_OK) {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "StorageInit: sqlite3_open fail: %s", sqlite3_errmsg(g_db_handle));
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        sqlite3_close(g_db_handle);
        pthread_mutex_destroy(&g_storage_lock);
        return -1;
    }

    // 开启 WAL 模式提升并发性能
    sqlite3_exec(g_db_handle, "PRAGMA journal_mode=WAL;", NULL, NULL, &err_msg);

    // 兼容旧数据库：先添加缺失的列，再创建表和索引
    sqlite3_exec(g_db_handle, "ALTER TABLE sensor_data ADD COLUMN rs485_json TEXT DEFAULT '[]';", NULL, NULL, NULL);
    sqlite3_exec(g_db_handle, "ALTER TABLE sensor_data ADD COLUMN can_json TEXT DEFAULT '[]';", NULL, NULL, NULL);
    sqlite3_exec(g_db_handle, "ALTER TABLE sensor_data ADD COLUMN synced INTEGER DEFAULT 0;", NULL, NULL, NULL);

    const char *create_sql =
        "CREATE TABLE IF NOT EXISTS sensor_data ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "timestamp DATETIME DEFAULT (datetime('now', 'localtime')),"
        "temperature REAL NOT NULL,"
        "humidity REAL NOT NULL,"
        "gas REAL NOT NULL DEFAULT 0,"
        "acc_xyz REAL NOT NULL DEFAULT 0,"
        "rs485_json TEXT DEFAULT '[]',"
        "can_json TEXT DEFAULT '[]',"
        "synced INTEGER DEFAULT 0);"
        "CREATE INDEX IF NOT EXISTS idx_timestamp ON sensor_data(timestamp);"
        "CREATE INDEX IF NOT EXISTS idx_synced ON sensor_data(synced);";

    ret = sqlite3_exec(g_db_handle, create_sql, NULL, NULL, &err_msg);
    if (ret != SQLITE_OK) {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "StorageInit: create table fail: %s", err_msg);
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        sqlite3_free(err_msg);
        sqlite3_close(g_db_handle);
        pthread_mutex_destroy(&g_storage_lock);
        return -1;
    }

    StorageInitAlarmTable();
    StorageInitThresholdTable();
    StorageInitUserTable();

    g_batch_count = 0;
    g_batch_last_flush = time(NULL);

    LogWrite(LOG_LEVEL_INFO, "StorageInit: sqlite3 init success");
    return 0;
}

int StorageDeInit(void) {
    StorageFlushBatch();

    pthread_mutex_lock(&g_storage_lock);
    if (g_db_handle != NULL) {
        sqlite3_close(g_db_handle);
        g_db_handle = NULL;
    }
    pthread_mutex_unlock(&g_storage_lock);
    pthread_mutex_destroy(&g_storage_lock);

    LogWrite(LOG_LEVEL_INFO, "StorageDeInit: sqlite3 deinit success");
    return 0;
}

int StorageInsertData(const Data_t *p_data) {
    if (p_data == NULL || g_db_handle == NULL) {
        LogWrite(LOG_LEVEL_ERROR, "StorageInsertData: invalid param");
        return -1;
    }

    int ret = 0;
    char insert_sql[1024] = {0};
    char rs485_json[512] = {0};
    char can_json[512] = {0};
    char *err_msg = NULL;

    pthread_mutex_lock(&g_storage_lock);

    serialize_rs485_json(p_data, rs485_json, sizeof(rs485_json));
    serialize_can_json(p_data, can_json, sizeof(can_json));
    snprintf(insert_sql, sizeof(insert_sql),
             "INSERT INTO sensor_data (temperature, humidity, gas, acc_xyz, rs485_json, can_json, synced) "
             "VALUES (%.2lf, %.2lf, %.2lf, %.2lf, '%s', '%s', 1);",
             p_data->Tem, p_data->Hum, p_data->Gas, p_data->Acc_xyz,
             rs485_json, can_json);

    ret = sqlite3_exec(g_db_handle, insert_sql, NULL, NULL, &err_msg);
    if (ret != SQLITE_OK) {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "StorageInsertData: insert fail: %s", err_msg);
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        sqlite3_free(err_msg);
        pthread_mutex_unlock(&g_storage_lock);
        return -1;
    }

    pthread_mutex_unlock(&g_storage_lock);
    return 0;
}

// 内部版本：调用者必须已持有 g_storage_lock
static int StorageFlushBatch_locked(void);

// 批量写入：数据先缓存，满或超时后事务提交
int StorageBatchInsert(const Data_t *p_data) {
    if (p_data == NULL) return -1;

    pthread_mutex_lock(&g_storage_lock);

    g_batch_buf[g_batch_count] = *p_data;
    g_batch_count++;

    int need_flush = 0;
    if (g_batch_count >= STORAGE_BATCH_SIZE) {
        need_flush = 1;
    } else {
        time_t now = time(NULL);
        if (now - g_batch_last_flush >= STORAGE_BATCH_TIMEOUT_SEC) {
            need_flush = 1;
        }
    }

    if (need_flush) {
        // 在锁内直接 flush，避免 unlock/flush 之间的竞态
        int ret = StorageFlushBatch_locked();
        pthread_mutex_unlock(&g_storage_lock);
        return ret;
    }

    pthread_mutex_unlock(&g_storage_lock);
    return 0;
}

static int StorageFlushBatch_locked(void) {
    if (g_db_handle == NULL || g_batch_count == 0) return 0;

    int ret = 0;
    char *err_msg = NULL;

    ret = sqlite3_exec(g_db_handle, "BEGIN TRANSACTION;", NULL, NULL, &err_msg);
    if (ret != SQLITE_OK) {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "StorageFlushBatch: BEGIN fail: %s", err_msg);
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        sqlite3_free(err_msg);
        return -1;
    }

    char insert_sql[1024] = {0};
    char rs485_json[512] = {0};
    char can_json[512] = {0};
    int success_count = 0;

    for (int i = 0; i < g_batch_count; i++) {
        serialize_rs485_json(&g_batch_buf[i], rs485_json, sizeof(rs485_json));
        serialize_can_json(&g_batch_buf[i], can_json, sizeof(can_json));
        snprintf(insert_sql, sizeof(insert_sql),
                 "INSERT INTO sensor_data (temperature, humidity, gas, acc_xyz, rs485_json, can_json, synced) "
                 "VALUES (%.2lf, %.2lf, %.2lf, %.2lf, '%s', '%s', 0);",
                 g_batch_buf[i].Tem, g_batch_buf[i].Hum,
                 g_batch_buf[i].Gas, g_batch_buf[i].Acc_xyz,
                 rs485_json, can_json);

        ret = sqlite3_exec(g_db_handle, insert_sql, NULL, NULL, &err_msg);
        if (ret != SQLITE_OK) {
            char log_buf[256] = {0};
            snprintf(log_buf, sizeof(log_buf), "StorageFlushBatch: insert[%d] fail: %s", i, err_msg);
            LogWrite(LOG_LEVEL_ERROR, log_buf);
            sqlite3_free(err_msg);
            err_msg = NULL;
        } else {
            success_count++;
        }
    }

    ret = sqlite3_exec(g_db_handle, "COMMIT;", NULL, NULL, &err_msg);
    if (ret != SQLITE_OK) {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "StorageFlushBatch: COMMIT fail: %s", err_msg);
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        sqlite3_free(err_msg);
        sqlite3_exec(g_db_handle, "ROLLBACK;", NULL, NULL, NULL);
    }

    char log_buf[256] = {0};
    snprintf(log_buf, sizeof(log_buf), "StorageFlushBatch: commit %d/%d records",
             success_count, g_batch_count);
    LogWrite(LOG_LEVEL_INFO, log_buf);

    g_batch_count = 0;
    g_batch_last_flush = time(NULL);
    return 0;
}

int StorageFlushBatch(void) {
    if (g_db_handle == NULL) return -1;

    pthread_mutex_lock(&g_storage_lock);
    int ret = StorageFlushBatch_locked();
    pthread_mutex_unlock(&g_storage_lock);
    return ret;
}

// WAL checkpoint: 将 WAL 日志合并回主数据库，防止 WAL 文件无限增长
int StorageCheckpoint(void)
{
    if (g_db_handle == NULL) return -1;

    pthread_mutex_lock(&g_storage_lock);
    sqlite3_exec(g_db_handle, "PRAGMA wal_checkpoint(PASSIVE);", NULL, NULL, NULL);
    pthread_mutex_unlock(&g_storage_lock);
    return 0;
}

int StorageQueryData(const char *start_time, const char *end_time, Data_t *p_result, int *p_count) {
    if (start_time == NULL || end_time == NULL || p_result == NULL || p_count == NULL || g_db_handle == NULL) {
        LogWrite(LOG_LEVEL_ERROR, "StorageQueryData: invalid param");
        return -1;
    }

    int ret = 0;
    char select_sql[512] = {0};
    char *err_msg = NULL;
    QueryCallbackParam_t cb_param = {0};

    cb_param.p_data_arr = p_result;
    cb_param.p_count = p_count;
    cb_param.max_count = *p_count;
    *p_count = 0;

    pthread_mutex_lock(&g_storage_lock);

    snprintf(select_sql, sizeof(select_sql),
             "SELECT * FROM sensor_data WHERE timestamp >= '%s' AND timestamp <= '%s' ORDER BY timestamp;",
             start_time, end_time);

    ret = sqlite3_exec(g_db_handle, select_sql, query_data_callback, &cb_param, &err_msg);
    if (ret != SQLITE_OK) {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "StorageQueryData: query fail: %s", err_msg);
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        sqlite3_free(err_msg);
        pthread_mutex_unlock(&g_storage_lock);
        return -1;
    }

    pthread_mutex_unlock(&g_storage_lock);
    return 0;
}

int StorageQueryDataEntries(const char *start_time, const char *end_time, DataEntry_t *p_result, int *p_count) {
    if (start_time == NULL || end_time == NULL || p_result == NULL || p_count == NULL || g_db_handle == NULL) {
        LogWrite(LOG_LEVEL_ERROR, "StorageQueryDataEntries: invalid param");
        return -1;
    }

    int ret = 0;
    char select_sql[512] = {0};
    char *err_msg = NULL;
    QueryEntryCallbackParam_t cb_param = {0};

    cb_param.p_entry_arr = p_result;
    cb_param.p_count = p_count;
    cb_param.max_count = *p_count;
    *p_count = 0;

    pthread_mutex_lock(&g_storage_lock);

    snprintf(select_sql, sizeof(select_sql),
             "SELECT id, timestamp, temperature, humidity, gas, acc_xyz "
             "FROM sensor_data WHERE timestamp >= '%s' AND timestamp <= '%s' ORDER BY timestamp;",
             start_time, end_time);

    ret = sqlite3_exec(g_db_handle, select_sql, query_entry_callback, &cb_param, &err_msg);
    if (ret != SQLITE_OK) {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "StorageQueryDataEntries: query fail: %s", err_msg);
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        sqlite3_free(err_msg);
        pthread_mutex_unlock(&g_storage_lock);
        return -1;
    }

    pthread_mutex_unlock(&g_storage_lock);
    return 0;
}

int StorageQueryUnsynced(DataEntry_t *p_result, int *p_count) {
    if (p_result == NULL || p_count == NULL || g_db_handle == NULL) {
        LogWrite(LOG_LEVEL_ERROR, "StorageQueryUnsynced: invalid param");
        return -1;
    }

    // 先 flush 未提交的批量数据
    StorageFlushBatch();

    int ret = 0;
    char select_sql[256] = {0};
    char *err_msg = NULL;
    UnsyncedCallbackParam_t cb_param = {0};

    cb_param.p_entry_arr = p_result;
    cb_param.p_count = p_count;
    cb_param.max_count = *p_count;
    *p_count = 0;

    pthread_mutex_lock(&g_storage_lock);

    snprintf(select_sql, sizeof(select_sql),
             "SELECT id, timestamp, temperature, humidity, gas, acc_xyz "
             "FROM sensor_data WHERE synced=0 ORDER BY timestamp;");

    ret = sqlite3_exec(g_db_handle, select_sql, unsynced_callback, &cb_param, &err_msg);
    if (ret != SQLITE_OK) {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "StorageQueryUnsynced: query fail: %s", err_msg);
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        sqlite3_free(err_msg);
        pthread_mutex_unlock(&g_storage_lock);
        return -1;
    }

    char log_buf[256] = {0};
    snprintf(log_buf, sizeof(log_buf), "StorageQueryUnsynced: found %d unsynced records", *p_count);
    LogWrite(LOG_LEVEL_INFO, log_buf);

    pthread_mutex_unlock(&g_storage_lock);
    return 0;
}

int StorageMarkSyncedAll(void) {
    if (g_db_handle == NULL) return -1;

    pthread_mutex_lock(&g_storage_lock);

    char *err_msg = NULL;
    int ret = sqlite3_exec(g_db_handle,
                           "UPDATE sensor_data SET synced=1 WHERE synced=0;",
                           NULL, NULL, &err_msg);
    if (ret != SQLITE_OK) {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "StorageMarkSyncedAll: fail: %s", err_msg);
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        sqlite3_free(err_msg);
        pthread_mutex_unlock(&g_storage_lock);
        return -1;
    }

    int changes = sqlite3_changes(g_db_handle);
    char log_buf[256] = {0};
    snprintf(log_buf, sizeof(log_buf), "StorageMarkSyncedAll: marked %d records as synced", changes);
    LogWrite(LOG_LEVEL_INFO, log_buf);

    pthread_mutex_unlock(&g_storage_lock);
    return 0;
}

int StorageMarkSyncedBatch(const int *ids, int count) {
    if (g_db_handle == NULL || ids == NULL || count <= 0) return -1;

    pthread_mutex_lock(&g_storage_lock);

    char sql[256] = {0};
    char *err_msg = NULL;
    int marked = 0;

    for (int i = 0; i < count; i++) {
        snprintf(sql, sizeof(sql), "UPDATE sensor_data SET synced=1 WHERE id=%d;", ids[i]);
        if (sqlite3_exec(g_db_handle, sql, NULL, NULL, &err_msg) == SQLITE_OK) {
            marked++;
        } else {
            sqlite3_free(err_msg);
            err_msg = NULL;
        }
    }

    char log_buf[128] = {0};
    snprintf(log_buf, sizeof(log_buf), "StorageMarkSyncedBatch: marked %d/%d", marked, count);
    LogWrite(LOG_LEVEL_INFO, log_buf);

    pthread_mutex_unlock(&g_storage_lock);
    return 0;
}

int StorageCleanOldData(int keep_days) {
    if (keep_days < 0 || g_db_handle == NULL) {
        LogWrite(LOG_LEVEL_ERROR, "StorageCleanOldData: invalid param");
        return -1;
    }

    pthread_mutex_lock(&g_storage_lock);

    char delete_sql[512] = {0};
    char *err_msg = NULL;

    snprintf(delete_sql, sizeof(delete_sql),
             "DELETE FROM sensor_data WHERE timestamp < datetime('now', 'localtime', '-%d day');",
             keep_days);

    int ret = sqlite3_exec(g_db_handle, delete_sql, NULL, NULL, &err_msg);
    if (ret != SQLITE_OK) {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "StorageCleanOldData: delete fail: %s", err_msg);
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        sqlite3_free(err_msg);
        pthread_mutex_unlock(&g_storage_lock);
        return -1;
    }

    pthread_mutex_unlock(&g_storage_lock);
    return 0;
}

// ============ 告警持久化 ============

int StorageInitAlarmTable(void) {
    if (g_db_handle == NULL) return -1;

    const char *sql =
        "CREATE TABLE IF NOT EXISTS alarm_log ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "timestamp DATETIME DEFAULT (datetime('now', 'localtime')),"
        "source TEXT NOT NULL,"
        "type TEXT NOT NULL,"
        "value REAL NOT NULL,"
        "threshold REAL NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_alarm_ts ON alarm_log(timestamp);";

    char *err_msg = NULL;
    int ret = sqlite3_exec(g_db_handle, sql, NULL, NULL, &err_msg);
    if (ret != SQLITE_OK) {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "StorageInitAlarmTable: fail: %s", err_msg);
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        sqlite3_free(err_msg);
        return -1;
    }
    return 0;
}

int StorageInsertAlarm(const char *timestamp, const char *source, const char *type,
                       double value, double threshold) {
    if (g_db_handle == NULL || timestamp == NULL || source == NULL || type == NULL) return -1;

    // 使用 trylock 避免阻塞 LVGL 定时器（storage 线程可能正持有锁做批量提交）
    if (pthread_mutex_trylock(&g_storage_lock) != 0)
        return -2;  // 锁被占用，跳过持久化（告警仍在内存中）

    char sql[512] = {0};
    char *err_msg = NULL;

    snprintf(sql, sizeof(sql),
             "INSERT INTO alarm_log (timestamp, source, type, value, threshold) "
             "VALUES ('%s', '%s', '%s', %.2lf, %.2lf);",
             timestamp, source, type, value, threshold);

    int ret = sqlite3_exec(g_db_handle, sql, NULL, NULL, &err_msg);
    if (ret != SQLITE_OK) {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "StorageInsertAlarm: fail: %s", err_msg);
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        sqlite3_free(err_msg);
    }

    pthread_mutex_unlock(&g_storage_lock);
    return (ret == SQLITE_OK) ? 0 : -1;
}

// ============ 阈值持久化 ============

int StorageInitThresholdTable(void) {
    if (g_db_handle == NULL) return -1;

    const char *sql =
        "CREATE TABLE IF NOT EXISTS alert_thresholds ("
        "id INTEGER PRIMARY KEY CHECK (id = 1),"
        "tem_max REAL DEFAULT 80.0, tem_min REAL DEFAULT 0.0,"
        "hum_max REAL DEFAULT 90.0, hum_min REAL DEFAULT 0.0,"
        "gas_max REAL DEFAULT 80.0, gas_min REAL DEFAULT 0.0,"
        "acc_max REAL DEFAULT 80.0, acc_min REAL DEFAULT 0.0,"
        "volt_max REAL DEFAULT 380.0, volt_min REAL DEFAULT 0.0,"
        "elec_max REAL DEFAULT 100.0, elec_min REAL DEFAULT 0.0,"
        "can_speed_max REAL DEFAULT 5000.0, can_speed_min REAL DEFAULT 0.0,"
        "can_flow_max REAL DEFAULT 200.0, can_flow_min REAL DEFAULT 0.0);";

    char *err_msg = NULL;
    int ret = sqlite3_exec(g_db_handle, sql, NULL, NULL, &err_msg);
    if (ret != SQLITE_OK) {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "StorageInitThresholdTable: fail: %s", err_msg);
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        sqlite3_free(err_msg);
        return -1;
    }

    // 兼容旧数据库：尝试添加可能缺失的列
    sqlite3_exec(g_db_handle, "ALTER TABLE alert_thresholds ADD COLUMN tem_min REAL DEFAULT 0.0;", NULL, NULL, NULL);
    sqlite3_exec(g_db_handle, "ALTER TABLE alert_thresholds ADD COLUMN hum_min REAL DEFAULT 0.0;", NULL, NULL, NULL);
    sqlite3_exec(g_db_handle, "ALTER TABLE alert_thresholds ADD COLUMN gas_min REAL DEFAULT 0.0;", NULL, NULL, NULL);
    sqlite3_exec(g_db_handle, "ALTER TABLE alert_thresholds ADD COLUMN acc_min REAL DEFAULT 0.0;", NULL, NULL, NULL);
    sqlite3_exec(g_db_handle, "ALTER TABLE alert_thresholds ADD COLUMN volt_min REAL DEFAULT 0.0;", NULL, NULL, NULL);
    sqlite3_exec(g_db_handle, "ALTER TABLE alert_thresholds ADD COLUMN elec_min REAL DEFAULT 0.0;", NULL, NULL, NULL);
    sqlite3_exec(g_db_handle, "ALTER TABLE alert_thresholds ADD COLUMN can_speed_min REAL DEFAULT 0.0;", NULL, NULL, NULL);
    sqlite3_exec(g_db_handle, "ALTER TABLE alert_thresholds ADD COLUMN can_flow_min REAL DEFAULT 0.0;", NULL, NULL, NULL);

    return 0;
}

int StorageLoadThreshold(double *tem_max, double *tem_min,
                         double *hum_max, double *hum_min,
                         double *gas_max, double *gas_min,
                         double *acc_max, double *acc_min,
                         double *volt_max, double *volt_min,
                         double *elec_max, double *elec_min,
                         double *can_speed_max, double *can_speed_min,
                         double *can_flow_max, double *can_flow_min) {
    if (g_db_handle == NULL || tem_max == NULL || tem_min == NULL ||
        hum_max == NULL || hum_min == NULL ||
        volt_max == NULL || volt_min == NULL ||
        elec_max == NULL || elec_min == NULL) return -1;

    sqlite3_stmt *stmt = NULL;
    int ret = sqlite3_prepare_v2(g_db_handle,
        "SELECT tem_max, tem_min, hum_max, hum_min, "
        "gas_max, gas_min, acc_max, acc_min, "
        "volt_max, volt_min, elec_max, elec_min, "
        "can_speed_max, can_speed_min, can_flow_max, can_flow_min "
        "FROM alert_thresholds WHERE id=1;",
        -1, &stmt, NULL);

    if (ret != SQLITE_OK || sqlite3_step(stmt) != SQLITE_ROW) {
        if (stmt) sqlite3_finalize(stmt);
        // 没有记录，插入默认值
        sqlite3_exec(g_db_handle,
            "INSERT OR IGNORE INTO alert_thresholds "
            "(id, tem_max, tem_min, hum_max, hum_min, gas_max, gas_min, acc_max, acc_min, "
            "volt_max, volt_min, elec_max, elec_min, "
            "can_speed_max, can_speed_min, can_flow_max, can_flow_min) "
            "VALUES (1, 80.0, 0.0, 90.0, 0.0, 80.0, 0.0, 80.0, 0.0, "
            "380.0, 0.0, 100.0, 0.0, 5000.0, 0.0, 200.0, 0.0);",
            NULL, NULL, NULL);
        *tem_max = 80.0; *tem_min = 0.0;
        *hum_max = 90.0; *hum_min = 0.0;
        *gas_max = 80.0; *gas_min = 0.0;
        *acc_max = 80.0; *acc_min = 0.0;
        *volt_max = 380.0; *volt_min = 0.0;
        *elec_max = 100.0; *elec_min = 0.0;
        *can_speed_max = 5000.0; *can_speed_min = 0.0;
        *can_flow_max = 200.0; *can_flow_min = 0.0;
        return 0;
    }

    *tem_max = sqlite3_column_double(stmt, 0);
    *tem_min = sqlite3_column_double(stmt, 1);
    *hum_max = sqlite3_column_double(stmt, 2);
    *hum_min = sqlite3_column_double(stmt, 3);
    *gas_max = sqlite3_column_double(stmt, 4);
    *gas_min = sqlite3_column_double(stmt, 5);
    *acc_max = sqlite3_column_double(stmt, 6);
    *acc_min = sqlite3_column_double(stmt, 7);
    *volt_max = sqlite3_column_double(stmt, 8);
    *volt_min = sqlite3_column_double(stmt, 9);
    *elec_max = sqlite3_column_double(stmt, 10);
    *elec_min = sqlite3_column_double(stmt, 11);
    *can_speed_max = sqlite3_column_double(stmt, 12);
    *can_speed_min = sqlite3_column_double(stmt, 13);
    *can_flow_max = sqlite3_column_double(stmt, 14);
    *can_flow_min = sqlite3_column_double(stmt, 15);

    sqlite3_finalize(stmt);
    return 0;
}

int StorageSaveThreshold(double tem_max, double tem_min,
                         double hum_max, double hum_min,
                         double gas_max, double gas_min,
                         double acc_max, double acc_min,
                         double volt_max, double volt_min,
                         double elec_max, double elec_min,
                         double can_speed_max, double can_speed_min,
                         double can_flow_max, double can_flow_min) {
    if (g_db_handle == NULL) return -1;

    char sql[1024] = {0};
    char *err_msg = NULL;

    pthread_mutex_lock(&g_storage_lock);

    snprintf(sql, sizeof(sql),
             "INSERT OR REPLACE INTO alert_thresholds "
             "(id, tem_max, tem_min, hum_max, hum_min, "
             "gas_max, gas_min, acc_max, acc_min, "
             "volt_max, volt_min, elec_max, elec_min, "
             "can_speed_max, can_speed_min, can_flow_max, can_flow_min) "
             "VALUES (1, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, "
             "%.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf);",
             tem_max, tem_min, hum_max, hum_min,
             gas_max, gas_min, acc_max, acc_min,
             volt_max, volt_min, elec_max, elec_min,
             can_speed_max, can_speed_min, can_flow_max, can_flow_min);

    int ret = sqlite3_exec(g_db_handle, sql, NULL, NULL, &err_msg);
    if (ret != SQLITE_OK) {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "StorageSaveThreshold: fail: %s", err_msg);
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        sqlite3_free(err_msg);
        pthread_mutex_unlock(&g_storage_lock);
        return -1;
    }

    pthread_mutex_unlock(&g_storage_lock);
    return 0;
}

// ============ 用户凭据持久化（XOR 加密 + hex 编码） ============

// 固定 XOR 密钥（仅防明文泄漏，非高强度加密）
static const char CRED_KEY[] = "LvGlEmBeD@2024#XoR";
#define CRED_KEY_LEN (sizeof(CRED_KEY) - 1)

static void cred_encrypt(const char *plain, char *cipher_out, int max_out)
{
    int i;
    for (i = 0; i < max_out - 1 && plain[i]; i++) {
        cipher_out[i] = plain[i] ^ CRED_KEY[i % CRED_KEY_LEN];
    }
    cipher_out[i] = '\0';
}

static void cred_decrypt(const char *cipher, char *plain_out, int max_out)
{
    cred_encrypt(cipher, plain_out, max_out);  // XOR 是对称的
}

// hex 编码（将二进制转为可见 ASCII，便于 SQL 存储）
static void cred_hex_encode(const char *raw, int raw_len, char *hex_out)
{
    int off = 0;
    for (int i = 0; i < raw_len; i++) {
        off += snprintf(hex_out + off, 3, "%02x", (unsigned char)raw[i]);
    }
    hex_out[off] = '\0';
}

static int cred_hex_decode(const char *hex, unsigned char *raw_out, int max_raw)
{
    int len = strlen(hex);
    if (len % 2) return -1;
    int raw_len = len / 2;
    if (raw_len > max_raw) raw_len = max_raw;
    for (int i = 0; i < raw_len; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        raw_out[i] = (unsigned char)byte;
    }
    return raw_len;
}

int StorageInitUserTable(void)
{
    if (g_db_handle == NULL) return -1;

    const char *sql =
        "CREATE TABLE IF NOT EXISTS user_credentials ("
        "username TEXT PRIMARY KEY,"
        "password_enc TEXT NOT NULL);";

    char *err_msg = NULL;
    pthread_mutex_lock(&g_storage_lock);
    int ret = sqlite3_exec(g_db_handle, sql, NULL, NULL, &err_msg);
    pthread_mutex_unlock(&g_storage_lock);

    if (ret != SQLITE_OK) {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "StorageInitUserTable: fail: %s", err_msg);
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        sqlite3_free(err_msg);
        return -1;
    }
    return 0;
}

int StorageSaveCredential(const char *username, const char *password)
{
    if (g_db_handle == NULL || username == NULL || password == NULL) return -1;

    // 1. XOR 加密
    char cipher[64] = {0};
    cred_encrypt(password, cipher, sizeof(cipher));

    // 2. hex 编码
    char hex[128] = {0};
    cred_hex_encode(cipher, strlen(cipher), hex);

    // 3. 存入数据库 (INSERT OR REPLACE)
    char sql[256] = {0};
    snprintf(sql, sizeof(sql),
             "INSERT OR REPLACE INTO user_credentials (username, password_enc) "
             "VALUES ('%s', '%s');",
             username, hex);

    char *err_msg = NULL;
    pthread_mutex_lock(&g_storage_lock);
    int ret = sqlite3_exec(g_db_handle, sql, NULL, NULL, &err_msg);
    pthread_mutex_unlock(&g_storage_lock);

    if (ret != SQLITE_OK) {
        char log_buf[256] = {0};
        snprintf(log_buf, sizeof(log_buf), "StorageSaveCredential: fail: %s", err_msg);
        LogWrite(LOG_LEVEL_ERROR, log_buf);
        sqlite3_free(err_msg);
        return -1;
    }
    return 0;
}

int StorageLoadCredential(const char *username, char *password, int max_len)
{
    if (g_db_handle == NULL || username == NULL || password == NULL || max_len <= 0)
        return -1;

    sqlite3_stmt *stmt = NULL;
    char sql[256] = {0};
    snprintf(sql, sizeof(sql),
             "SELECT password_enc FROM user_credentials WHERE username='%s';",
             username);

    pthread_mutex_lock(&g_storage_lock);
    int ret = sqlite3_prepare_v2(g_db_handle, sql, -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        pthread_mutex_unlock(&g_storage_lock);
        return -1;
    }

    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&g_storage_lock);
        return -1;  // 用户不存在
    }

    const char *hex = (const char *)sqlite3_column_text(stmt, 0);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_storage_lock);

    if (hex == NULL) return -1;

    // hex 解码
    unsigned char cipher[64] = {0};
    int cipher_len = cred_hex_decode(hex, cipher, sizeof(cipher) - 1);
    if (cipher_len < 0) return -1;
    cipher[cipher_len] = '\0';

    // XOR 解密
    cred_decrypt((const char *)cipher, password, max_len);
    return 0;
}
