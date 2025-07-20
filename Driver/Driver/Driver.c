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
BOOLEAN gShutdownThread = FALSE; // ������ ���� ��ȣ �߰�

VOID CommunicationThread(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    WriteLog("[%s] Thread Started.\r\n", __func__);

    NTSTATUS status;
    __try {
        // gShutdownThread üũ �߰�
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

            // Ÿ�Ӿƿ��� �߰��Ͽ� �ֱ������� ���� ��ȣ üũ
            LARGE_INTEGER timeout;
            timeout.QuadPart = -10000000LL; // 1�� Ÿ�Ӿƿ� (100ns ����)
            
            status = KeWaitForSingleObject(writeEventObj, Executive, KernelMode, FALSE, &timeout);
            
            if (status == STATUS_TIMEOUT) {
                ObDereferenceObject(writeEventObj);
                continue; // Ÿ�Ӿƿ��� ���� ���, ���� ��ȣ üũ
            }
            
            if (!NT_SUCCESS(status) || gShutdownThread) {
                WriteLog("[%s] Thread termination requested or wait failed.\r\n", __func__);
                ObDereferenceObject(writeEventObj);
                break;
            }

            WriteLog("[%s] gWriteEvent triggered.\r\n", __func__);

            // ���� �޸� �۾�
            char buf[SHARED_MEM_SIZE];
            SIZE_T returnSize = 0;
            status = MmCopyVirtualMemory(PsGetCurrentProcess(), gSharedMemory, PsGetCurrentProcess(), buf, SHARED_MEM_SIZE, KernelMode, &returnSize);
            if (!NT_SUCCESS(status)) {
                WriteLog("[%s] Failed to copy shared memory. status=0x%x.\r\n", __func__, status);
            }
            else {
                WriteLog("[%s] Message From Client: %s\r\n", __func__, buf);

                _strcat(buf, " - Processed by Driver");

                // ������ buf�� ���� �޸𸮿� �ٽ� ����
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

            // ���� ��ȣ ��Ȯ��
            if (gShutdownThread) {
                break;
            }

            // ������忡 ó�� �ϷḦ �˸�
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

    // ������ ���� ó��
    if (gThreadHandle) {
        // ���� ��ȣ ����
        gShutdownThread = TRUE;
        
        PKTHREAD threadObject = NULL;
        NTSTATUS status = ObReferenceObjectByHandle(gThreadHandle,
            THREAD_ALL_ACCESS,
            *PsThreadType,
            KernelMode,
            (PVOID*)&threadObject,
            NULL);
        
        if (NT_SUCCESS(status)) {
            // writeEvent�� ��ȣ ���·� ����� ��� ���� �����带 ����
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
            
            // �����尡 ����� ������ ��� (Ÿ�Ӿƿ� �߰�)
            LARGE_INTEGER timeout;
            timeout.QuadPart = -50000000LL; // 5�� Ÿ�Ӿƿ�
            
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

        // 1. ���� �޸� ���� ����/���� (�̸�: "\\BaseNamedObjects\\SharedMemorySection")
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

        // ObReferenceObjectByHandle�� ���� ���� ��ü�� �����͸� ����
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

        // ����: pSectionObject�� ù��° ���ڷ� ���
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


        // 2. �̺�Ʈ ���� (�̸�: "\\BaseNamedObjects\\EventWriteToDriver", "\\BaseNamedObjects\\EventReadFromDriver")
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


        // 3. ��� ������ ����
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