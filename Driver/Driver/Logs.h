#pragma once

#include <ntddk.h>
#include <stdarg.h>
#include <wdm.h>
#include <ntstrsafe.h>  // ������ ���ڿ� �Լ� ����

#define LOG_FILE_PATH L"\\??\\C:\\kernel_log.txt"

void WriteLogToFile(const char* Format, ...);