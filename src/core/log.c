#include "log.h"
#include <time.h>
#include <string.h>
#include <dirent.h>

char filename_log[256];
char filepath_log[256];
FILE *flog;
LogLevel_t CurLogLevel = LOG_LEVEL_TRACE;
pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

// 日志模块初始化：创建/打开日志文件
int LogInit(char *pfilepath)
{
    time_t tm;
    struct tm *ptm = NULL;

    time(&tm);
    ptm = localtime(&tm);

    sprintf(filepath_log, "%s", pfilepath);
    sprintf(filename_log, "%s/%04d.%02d.%02d", pfilepath, ptm->tm_year+1900, ptm->tm_mon+1, ptm->tm_mday);

    flog = fopen(filename_log, "a+");
    if(NULL == flog)
    {
        LogWrite(LOG_LEVEL_ERROR, "LogInit: open log file failed");
        return -1;
    }
    LogWrite(LOG_LEVEL_INFO, "LogInit: log file open success"); 
    return 0;
}

// 日志模块反初始化：关闭日志文件
int LogDeInit(void)
{
    if(flog != NULL)
    {
        fclose(flog);
        flog = NULL;
        LogWrite(LOG_LEVEL_INFO, "LogDeInit: log file close success");
    }

    pthread_mutex_destroy(&g_log_lock);
    return 0;
}

// 设置当前日志级别
int SetCurLogLevel(LogLevel_t level)
{
    CurLogLevel = level;
    char logMsg[64] = {0};
    sprintf(logMsg, "SetCurLogLevel: log level set to %d", level);
    LogWrite(LOG_LEVEL_INFO, logMsg); 
    return 0;
}

// 清理过期日志文件（保留指定天数内的日志）
int LogCleanOldFiles(int keepDays)
{
    DIR *dir;
    struct dirent *entry;
    char fullpath[512];
    time_t now = time(NULL);
    struct tm *today = localtime(&now);
    // 计算保留日期的时间戳（当天 00:00:00 - keepDays 天）
    today->tm_hour = 0; today->tm_min = 0; today->tm_sec = 0;
    time_t keepTime = mktime(today) - keepDays * 24 * 3600;

    dir = opendir(filepath_log);
    if(dir == NULL) {
        LogWrite(LOG_LEVEL_ERROR, "LogCleanOldFiles: open log dir failed"); 
        return -1;
    }

    while((entry = readdir(dir)) != NULL) {
        // 跳过 . 和 .. 目录
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        // 检查文件名是否符合 YYYY.MM.DD 格式（简单判断：10个字符+数字+点）
        int year, month, day;
        if(sscanf(entry->d_name, "%4d.%2d.%2d", &year, &month, &day) == 3) {
            // 构建文件完整路径
            snprintf(fullpath, sizeof(fullpath), "%s/%s", filepath_log, entry->d_name);

            struct tm file_tm = {0};
            file_tm.tm_year = year - 1900;
            file_tm.tm_mon  = month - 1;
            file_tm.tm_mday = day;
            file_tm.tm_hour = 0; file_tm.tm_min = 0; file_tm.tm_sec = 0;
            time_t fileTime = mktime(&file_tm);

            if(fileTime < keepTime) {
                if(remove(fullpath) == 0) {
                    char successMsg[256] = {0};
                    sprintf(successMsg, "LogCleanOldFiles: delete old log success, file=%s", fullpath);
                    LogWrite(LOG_LEVEL_INFO, successMsg); 
                } else {
                    char errMsg[256] = {0};
                    sprintf(errMsg, "LogCleanOldFiles: delete old log failed, file=%s", fullpath);
                    LogWrite(LOG_LEVEL_ERROR, errMsg);
                }
            }
        }
    }
    closedir(dir);
    LogWrite(LOG_LEVEL_INFO, "LogCleanOldFiles: clean old log finish");
    return 0;
}