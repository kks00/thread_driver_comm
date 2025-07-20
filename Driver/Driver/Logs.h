#pragma once

#include <ntddk.h>
#include <stdarg.h>
#include <wdm.h>
#include <ntstrsafe.h>  // 안전한 문자열 함수 지원

// #define DEBUG_MODE

// #define LOG_TO_FILE
#define LOG_FILE_PATH L"\\??\\C:\\kernel_log.txt"

#ifdef DEBUG_MODE
void WriteLog(const char* Format, ...);
#else
#define WriteLog(...)
#endif