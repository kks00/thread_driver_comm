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
HANDLE gThreadHandle = NULL;
BOOLEAN gShutdownThread = FALSE; // 스레드 종료 신호 추가

VOID CommunicationThread(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    WriteLog("[%s] Thread Started.\r\n", __func__);

    NTSTATUS status;
    __try {
        // gShutdownThread 체크 추가
        while (!gShutdownThread) {
            PKEVENT writeEventObj = NULL;
            status = ObReferenceObjectByHandle(gWriteEvent,
                EVENT_MODIFY_STATE,
                *ExEventObjectType,
                KernelMode,
                (PVOID*)&writeEventObj,
                NULL);
            if (!NT_SUCCESS(status)) {
                WriteLog("[%s] Failed to reference gWriteEvent. status=0x%X\r\n", __func__, status);
                break;
            }

            // 타임아웃을 추가하여 주기적으로 종료 신호 체크
            LARGE_INTEGER timeout;
            timeout.QuadPart = -10000000LL; // 1초 타임아웃 (100ns 단위)
            
            status = KeWaitForSingleObject(writeEventObj, Executive, KernelMode, FALSE, &timeout);
            
            if (status == STATUS_TIMEOUT) {
                ObDereferenceObject(writeEventObj);
                continue; // 타임아웃시 루프 계속, 종료 신호 체크
            }
            
            if (!NT_SUCCESS(status) || gShutdownThread) {
                WriteLog("[%s] Thread termination requested or wait failed.\r\n", __func__);
                ObDereferenceObject(writeEventObj);
                break;
            }

            WriteLog("[%s] gWriteEvent triggered.\r\n", __func__);

            // 공유 메모리 작업
            char buf[SHARED_MEM_SIZE];
            SIZE_T returnSize = 0;
            status = MmCopyVirtualMemory(PsGetCurrentProcess(), gSharedMemory, PsGetCurrentProcess(), buf, SHARED_MEM_SIZE, KernelMode, &returnSize);
            if (!NT_SUCCESS(status)) {
                WriteLog("[%s] Failed to copy shared memory. status=0x%x.\r\n", __func__, status);
            }
            else {
                WriteLog("[%s] Message From Client: %s\r\n", __func__, buf);

                _strcat(buf, " - Processed by Driver");

                // 수정된 buf를 공유 메모리에 다시 복사
                SIZE_T writeSize = 0;
                status = MmCopyVirtualMemory(PsGetCurrentProcess(), buf, PsGetCurrentProcess(), gSharedMemory, SHARED_MEM_SIZE, KernelMode, &writeSize);
                if (!NT_SUCCESS(status)) {
                    WriteLog("[%s] Failed to write back to shared memory. status=0x%x.\r\n", __func__, status);
                }
                else {
                    WriteLog("[%s] Successfully wrote back to shared memory. writeSize=%llu\r\n", __func__, writeSize);
                }
            }

            ObDereferenceObject(writeEventObj);

            // 종료 신호 재확인
            if (gShutdownThread) {
                break;
            }

            // 유저모드에 처리 완료를 알림
            PKEVENT readEventObj = NULL;
            status = ObReferenceObjectByHandle(gReadEvent,
                EVENT_MODIFY_STATE,
                *ExEventObjectType,
                KernelMode,
                (PVOID*)&readEventObj,
                NULL);
            if (!NT_SUCCESS(status)) {
                WriteLog("[%s] Failed to reference gReadEvent. status=0x%X\r\n", __func__, status);
                break;
            }

            KeSetEvent(readEventObj, IO_NO_INCREMENT, FALSE);
            ObDereferenceObject(readEventObj);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLog("[%s] Exception occurred in CommunicationThread.\r\n", __func__);
    }

    WriteLog("[%s] Communication thread exiting.\r\n", __func__);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    WriteLog("[%s] Unloading driver.\r\n", __func__);

    // 스레드 종료 처리
    if (gThreadHandle) {
        // 종료 신호 설정
        gShutdownThread = TRUE;
        
        PKTHREAD threadObject = NULL;
        NTSTATUS status = ObReferenceObjectByHandle(gThreadHandle,
            THREAD_ALL_ACCESS,
            *PsThreadType,
            KernelMode,
            (PVOID*)&threadObject,
            NULL);
        
        if (NT_SUCCESS(status)) {
            // writeEvent를 신호 상태로 만들어 대기 중인 스레드를 깨움
            if (gWriteEvent) {
                PKEVENT writeEventObj = NULL;
                NTSTATUS eventStatus = ObReferenceObjectByHandle(gWriteEvent,
                    EVENT_MODIFY_STATE,
                    *ExEventObjectType,
                    KernelMode,
                    (PVOID*)&writeEventObj,
                    NULL);
                if (NT_SUCCESS(eventStatus)) {
                    KeSetEvent(writeEventObj, IO_NO_INCREMENT, FALSE);
                    ObDereferenceObject(writeEventObj);
                }
            }
            
            // 스레드가 종료될 때까지 대기 (타임아웃 추가)
            LARGE_INTEGER timeout;
            timeout.QuadPart = -50000000LL; // 5초 타임아웃
            
            status = KeWaitForSingleObject(threadObject, Executive, KernelMode, FALSE, &timeout);
            if (status == STATUS_TIMEOUT) {
                WriteLog("[%s] Thread termination timeout.\r\n", __func__);
            } else {
                WriteLog("[%s] Communication thread terminated.\r\n", __func__);
            }
            
            ObDereferenceObject(threadObject);
        }
        
        ZwClose(gThreadHandle);
        gThreadHandle = NULL;
        WriteLog("[%s] Closed thread handle.\r\n", __func__);
    }

    if (gSharedMemory) {
        MmUnmapViewInSystemSpace(gSharedMemory);
        gSharedMemory = NULL;
        WriteLog("[%s] Unmapped shared memory.\r\n", __func__);
    }

    if (gSectionHandle) {
        ZwClose(gSectionHandle);
        gSectionHandle = NULL;
        WriteLog("[%s] Closed section handle.\r\n", __func__);
    }

    if (gWriteEvent) {
        ZwClose(gWriteEvent);
        gWriteEvent = NULL;
        WriteLog("[%s] Closed write event handle.\r\n", __func__);
    }

    if (gReadEvent) {
        ZwClose(gReadEvent);
        gReadEvent = NULL;
        WriteLog("[%s] Closed read event handle.\r\n", __func__);
    }

    WriteLog("[%s] Driver unloaded successfully.\r\n", __func__);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registryPath) {
    UNREFERENCED_PARAMETER(driver);
    UNREFERENCED_PARAMETER(registryPath);

    if (driver) {
        driver->DriverUnload = DriverUnload;
    }

    WriteLog("[%s] Driver Loaded.\r\n", __func__);
    WriteLog("[%s] Driver Object: %p, Registry Path: %wZ\r\n", __func__, driver, registryPath);

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
            WriteLog("[%s] Failed ZwCreateSection. status=0x%X\r\n", __func__, status);
            return status;
        }
        WriteLog("[%s] Created shared memory. status=0x%X, Handle=%p\r\n", __func__, status, gSectionHandle);

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
            WriteLog("[%s] Failed ObReferenceObjectByHandle for gSectionHandle. status=0x%X\r\n", __func__, status);
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
            WriteLog("[%s] Failed MmMapViewInSystemSpace while mapping shared memory. status=0x%X\r\n", __func__, status);
            return status;
        }
        WriteLog("[%s] Mapped shared memory. ptr=%p, viewSize=%llu\r\n", __func__, gSharedMemory, viewSize);


        // 2. 이벤트 생성 (이름: "\\BaseNamedObjects\\EventWriteToDriver", "\\BaseNamedObjects\\EventReadFromDriver")
        UNICODE_STRING writeEventName = RTL_CONSTANT_STRING(L"\\BaseNamedObjects\\EventWriteToDriver");
        UNICODE_STRING readEventName = RTL_CONSTANT_STRING(L"\\BaseNamedObjects\\EventReadFromDriver");

        InitializeObjectAttributes(&objAttrs, &writeEventName, OBJ_KERNEL_HANDLE | OBJ_OPENIF, NULL, NULL);
        status = ZwCreateEvent(&gWriteEvent, EVENT_ALL_ACCESS, &objAttrs, SynchronizationEvent, FALSE);
        if (!NT_SUCCESS(status)) {
            WriteLog("[%s] Failed ZwCreateEvent for gWriteEvent. status=0x%X\r\n", __func__, status);
            return status;
        }
        WriteLog("[%s] Created EventWriteToDriver. Handle=%p\r\n", __func__, gWriteEvent);

        InitializeObjectAttributes(&objAttrs, &readEventName, OBJ_KERNEL_HANDLE | OBJ_OPENIF, NULL, NULL);
        status = ZwCreateEvent(&gReadEvent, EVENT_ALL_ACCESS, &objAttrs, SynchronizationEvent, FALSE);
        if (!NT_SUCCESS(status)) {
            WriteLog("[%s] Failed ZwCreateEvent for gReadEvent. status=0x%X\r\n", __func__, status);
            return status;
        }
        WriteLog("[%s] Created EventReadFromDriver. Handle=%p\r\n", __func__, gReadEvent);


        // 3. 통신 스레드 생성
        status = PsCreateSystemThread(&gThreadHandle, THREAD_ALL_ACCESS, NULL,
            NULL, NULL, CommunicationThread, NULL);
        if (!NT_SUCCESS(status)) {
            WriteLog("[%s] Failed PsCreateSystemThread. status=0x%X\r\n", __func__, status);
            return status;
        }

        WriteLog("[%s] Driver loaded successfully.\r\n", __func__);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLog("[%s] Exception while initializing driver.\r\n", __func__);
    }

    return STATUS_SUCCESS;
}