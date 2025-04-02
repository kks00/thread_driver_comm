#include <ntifs.h>
#include <ntddk.h>
#include <windef.h>
#include <ntimage.h>

#include "Logs.h"
#include "util.h"

#define SHARED_MEM_SIZE 1024

extern POBJECT_TYPE* MmSectionObjectType;
PVOID gSharedMemory = NULL;
HANDLE gSectionHandle = NULL;
HANDLE gWriteEvent = NULL;
HANDLE gReadEvent = NULL;

VOID CommunicationThread(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    WriteLogToFile("[%s] Thread Started.\r\n", __func__);

    NTSTATUS status;
    __try {
        for (;;) {
            PKEVENT writeEventObj = NULL;
            status = ObReferenceObjectByHandle(gWriteEvent,
                EVENT_MODIFY_STATE,
                *ExEventObjectType,
                KernelMode,
                (PVOID*)&writeEventObj,
                NULL);
            if (!NT_SUCCESS(status)) {
                WriteLogToFile("[%s] Failed to reference gWriteEvent. status=0x%X\r\n", __func__, status);
                break;
            }

            status = KeWaitForSingleObject(writeEventObj, Executive, KernelMode, FALSE, NULL);
            if (!NT_SUCCESS(status)) {
                WriteLogToFile("[%s] Failed KeWaitForSingleObject while waiting gWriteEventObj.\r\n", __func__);
                break;
            }
            KeResetEvent(writeEventObj);
            WriteLogToFile("[%s] gWriteEvent triggered.\r\n", __func__);

            ObDereferenceObject(writeEventObj);


            // 공유 메모리에서 데이터를 읽고 처리 (예: 로그 출력 등)
            char buf[SHARED_MEM_SIZE];
            SIZE_T returnSize = 0;
            status = MmCopyVirtualMemory(PsGetCurrentProcess(), gSharedMemory, PsGetCurrentProcess(), buf, SHARED_MEM_SIZE, KernelMode, &returnSize);
			if (!NT_SUCCESS(status)) {
				WriteLogToFile("[%s] Failed to copy shared memory. status=0x%x.\r\n", __func__, status);
            }
            else {
                WriteLogToFile("[%s] Message From Client: %s\r\n", __func__, buf);
                _strcat(gSharedMemory, " - Processed by Driver");
            }

            // 유저모드에 처리 완료를 알림 (gReadEventObj 사용)
            PKEVENT readEventObj = NULL;
            status = ObReferenceObjectByHandle(gReadEvent,
                EVENT_MODIFY_STATE,
                *ExEventObjectType,
                KernelMode,
                (PVOID*)&readEventObj,
                NULL);
            if (!NT_SUCCESS(status)) {
                WriteLogToFile("[%s] Failed to reference gReadEvent. status=0x%X\r\n", __func__, status);
                break;
            }

            KeSetEvent(readEventObj, IO_NO_INCREMENT, FALSE);

            ObDereferenceObject(readEventObj);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLogToFile("[%s] Exception occurred in CommunicationThread.\r\n", __func__);
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    WriteLogToFile("[%s] Unloading driver.\r\n", __func__);

    if (gSharedMemory) {
        MmUnmapViewInSystemSpace(gSharedMemory);
        gSharedMemory = NULL;
        WriteLogToFile("[%s] Unmapped shared memory.\r\n", __func__);
    }

    if (gSectionHandle) {
        ZwClose(gSectionHandle);
        gSectionHandle = NULL;
        WriteLogToFile("[%s] Closed section handle.\r\n", __func__);
    }

    if (gWriteEvent) {
        ZwClose(gWriteEvent);
        gWriteEvent = NULL;
        WriteLogToFile("[%s] Closed write event handle.\r\n", __func__);
    }

    if (gReadEvent) {
        ZwClose(gReadEvent);
        gReadEvent = NULL;
        WriteLogToFile("[%s] Closed read event handle.\r\n", __func__);
    }

    WriteLogToFile("[%s] Driver unloaded successfully.\r\n", __func__);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registryPath) {
    UNREFERENCED_PARAMETER(driver);
    UNREFERENCED_PARAMETER(registryPath);

    if (driver) {
        driver->DriverUnload = DriverUnload;
    }

    WriteLogToFile("[%s] Driver Loaded.\r\n", __func__);
    WriteLogToFile("[%s] Driver Object: %p, Registry Path: %wZ\r\n", __func__, driver, registryPath);

    __try {
        NTSTATUS status;
        OBJECT_ATTRIBUTES objAttrs;

        // 1. 공유 메모리 섹션 생성/오픈 (이름: "\\BaseNamedObjects\\SharedMemorySection")
        UNICODE_STRING sectionName = RTL_CONSTANT_STRING(L"\\BaseNamedObjects\\SharedMemorySection");
        LARGE_INTEGER sectionSize;
        sectionSize.QuadPart = SHARED_MEM_SIZE;

        InitializeObjectAttributes(&objAttrs, &sectionName, OBJ_KERNEL_HANDLE | OBJ_OPENIF, NULL, NULL);
        status = ZwCreateSection(&gSectionHandle,
            SECTION_ALL_ACCESS,
            &objAttrs,
            &sectionSize,
            PAGE_READWRITE,
            SEC_COMMIT,
            NULL);
        if (!NT_SUCCESS(status)) {
            WriteLogToFile("[%s] Failed ZwCreateSection. status=0x%X\r\n", __func__, status);
            return status;
        }
        WriteLogToFile("[%s] Created shared memory. status=0x%X, Handle=%p\r\n", __func__, status, gSectionHandle);

        // ObReferenceObjectByHandle를 통해 섹션 객체의 포인터를 얻음
        PVOID pSectionObject = NULL;
        status = ObReferenceObjectByHandle(gSectionHandle,
            SECTION_ALL_ACCESS,
            *MmSectionObjectType,
            KernelMode,
            &pSectionObject,
            NULL);
        if (!NT_SUCCESS(status)) {
            ZwClose(gSectionHandle);
            WriteLogToFile("[%s] Failed ObReferenceObjectByHandle for gSectionHandle. status=0x%X\r\n", __func__, status);
            return status;
        }

        // 맵핑: pSectionObject를 첫번째 인자로 사용
        SIZE_T viewSize = SHARED_MEM_SIZE;
        status = MmMapViewInSystemSpace(pSectionObject,
            &gSharedMemory,
            &viewSize);
        ObDereferenceObject(pSectionObject);
        if (!NT_SUCCESS(status)) {
            ZwClose(gSectionHandle);
            WriteLogToFile("[%s] Failed MmMapViewInSystemSpace while mapping shared memory. status=0x%X\r\n", __func__, status);
            return status;
        }
        WriteLogToFile("[%s] Mapped shared memory. ptr=%p, viewSize=%llu\r\n", __func__, gSharedMemory, viewSize);


        // 2. 이벤트 생성 (이름: "\\BaseNamedObjects\\EventWriteToDriver", "\\BaseNamedObjects\\EventReadFromDriver")
        UNICODE_STRING writeEventName = RTL_CONSTANT_STRING(L"\\BaseNamedObjects\\EventWriteToDriver");
        UNICODE_STRING readEventName = RTL_CONSTANT_STRING(L"\\BaseNamedObjects\\EventReadFromDriver");

        InitializeObjectAttributes(&objAttrs, &writeEventName, OBJ_KERNEL_HANDLE | OBJ_OPENIF, NULL, NULL);
        status = ZwCreateEvent(&gWriteEvent, EVENT_ALL_ACCESS, &objAttrs, NotificationEvent, FALSE);
        if (!NT_SUCCESS(status)) {
            WriteLogToFile("[%s] Failed ZwCreateEvent for gWriteEvent. status=0x%X\r\n", __func__, status);
            return status;
        }
        WriteLogToFile("[%s] Created EventWriteToDriver. Handle=%p\r\n", __func__, gWriteEvent);

        InitializeObjectAttributes(&objAttrs, &readEventName, OBJ_KERNEL_HANDLE | OBJ_OPENIF, NULL, NULL);
        status = ZwCreateEvent(&gReadEvent, EVENT_ALL_ACCESS, &objAttrs, NotificationEvent, FALSE);
        if (!NT_SUCCESS(status)) {
            WriteLogToFile("[%s] Failed ZwCreateEvent for gReadEvent. status=0x%X\r\n", __func__, status);
            return status;
        }
        WriteLogToFile("[%s] Created EventReadFromDriver. Handle=%p\r\n", __func__, gReadEvent);


        // 3. 통신 스레드 생성
        HANDLE threadHandle;
        status = PsCreateSystemThread(&threadHandle, THREAD_ALL_ACCESS, NULL, 
                                      NULL, NULL, CommunicationThread, NULL);
        if (!NT_SUCCESS(status)) {
            WriteLogToFile("[%s] Failed PsCreateSystemThread. status=0x%X\r\n", __func__, status);
            return status;
        }
        ZwClose(threadHandle);

        WriteLogToFile("[%s] Driver loaded successfully.\r\n", __func__);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLogToFile("[%s] Exception while initializing driver.\r\n", __func__);
    }

    return STATUS_SUCCESS;
}