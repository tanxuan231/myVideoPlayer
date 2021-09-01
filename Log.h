#ifndef LOG_H
#define LOG_H

#include <QDebug>
#include <pthread.h>

static bool const debugOpen = false;
//static bool const debugOpen = true;

#define LogDebug(format, ...) \
    do { \
        if (debugOpen) { \
            qDebug("[DEBUG][%s:%s:%d][%ld]" format, __FILE__, __FUNCTION__, __LINE__, pthread_self(), ##__VA_ARGS__); \
        } \
    } while(false)
#define LogInfo(format, ...) qInfo("[INFO][%s:%s:%d][%ld]" format, __FILE__, __FUNCTION__, __LINE__, pthread_self(), ##__VA_ARGS__)
#define LogWarn(format, ...) qWarning("[WARN][%s:%s:%d][%ld]" format, __FILE__, __FUNCTION__, __LINE__, pthread_self(), ##__VA_ARGS__)
#define LogError(format, ...) qDebug("[ERROR][%s:%s:%d][%ld]" format, __FILE__, __FUNCTION__, __LINE__, pthread_self(), ##__VA_ARGS__)

#endif // LOG_H
