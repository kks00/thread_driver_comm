#pragma once

#include <ntddk.h>
#include <stdarg.h>
#include <wdm.h>
#include <ntstrsafe.h>  // ������ ���ڿ� �Լ� ����

#define DEBUG_MODE

// #define LOG_TO_FILE
#define LOG_FILE_PATH L"\\??\\C:\\kernel_log.txt"

void WriteLog(const char* Format, ...);