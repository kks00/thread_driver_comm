#pragma once

#include <ntddk.h>
#include <stdarg.h>
#include <wdm.h>
#include <ntstrsafe.h>  // 안전한 문자열 함수 지원

#define LOG_FILE_PATH L"\\??\\C:\\kernel_log.txt"

void WriteLogToFile(const char* Format, ...);