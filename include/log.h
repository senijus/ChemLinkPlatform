#ifndef __LOG_H__
#define __LOG_H__

typedef enum LogLevel
{
    LOG_LEVEL_FATAL,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_TRACE,
}LogLevel_t;

#include <stdio.h>
#include <time.h>
#include <pthread.h>

extern char filename_log[256];
extern char filepath_log[256];
extern FILE *flog;
extern LogLevel_t CurLogLevel;
extern pthread_mutex_t g_log_lock;

extern int LogInit(char *pfilepath);
extern int LogDeInit(void);
extern int SetCurLogLevel(LogLevel_t level);
extern int LogCleanOldFiles(int keepDays);

#define LogWrite(level, pmsg)\
do\
{\
    time_t _log_tm;\
    char __tmpbuff__[256] = {0};\
    struct tm *_log_ptm;\
\
    pthread_mutex_lock(&g_log_lock);\
\
    time(&_log_tm);\
    _log_ptm = localtime(&_log_tm);\
\
    sprintf(__tmpbuff__, "%s/%04d.%02d.%02d", filepath_log, _log_ptm->tm_year+1900, _log_ptm->tm_mon+1, _log_ptm->tm_mday);\
\
    if(strcmp(filename_log, __tmpbuff__) < 0)\
    {\
        sprintf(filename_log, "%s", __tmpbuff__);\
\
        fclose(flog);\
\
        flog = fopen(filename_log, "a+");\
    }\
\
    if(level <= CurLogLevel && flog != NULL)\
    {\
        fprintf(flog, "[%04d-%02d-%02d %02d:%02d:%02d][%s:%d]:%s\n", _log_ptm->tm_year+1900, _log_ptm->tm_mon+1, _log_ptm->tm_mday,\
                                                                    _log_ptm->tm_hour, _log_ptm->tm_min, _log_ptm->tm_sec,\
                                                                    __FILE__, __LINE__, pmsg);\
        fflush(flog);\
    }\
\
    pthread_mutex_unlock(&g_log_lock);\
\
}while(0)

#endif