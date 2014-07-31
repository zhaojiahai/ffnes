/* 包含头文件 */
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include "log.h"

// 内部常量定义
#define LOG_MODE_FILE     0
#define LOG_MODE_DEBUGER  1

// 内部全部变量定义
static FILE *s_log_fp   = NULL;
static DWORD s_log_mode = 0;

/* 函数实现 */
void log_init(char *file)
{
    if (!s_log_fp) {
        if (strcmp(file, "DEBUGER") == 0) {
            s_log_mode = LOG_MODE_DEBUGER;
        }
        else {
            s_log_mode = LOG_MODE_FILE;
            s_log_fp   = fopen(file, "w");
        }
    }
}

void log_done(void)
{
    if (s_log_fp) {
        fflush(s_log_fp);
        fclose(s_log_fp);
        s_log_fp   = NULL;
        s_log_mode = 0;
    }
}

#define MAX_LOG_BUF 1024
void log_printf(char *format, ...)
{
    char buf[MAX_LOG_BUF];
    va_list valist;

    va_start(valist, format);
    vsnprintf(buf, MAX_LOG_BUF, format, valist);
    va_end(valist);

    switch (s_log_mode)
    {
    case LOG_MODE_FILE:
        fputs(buf, s_log_fp);
        fflush(s_log_fp);
        break;

    case LOG_MODE_DEBUGER:
        OutputDebugString(buf);
        break;
    }
}
