#include "Logs.h"

void WriteLogToFile(const char* Format, ...) {
    UNICODE_STRING FilePath;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle;
    NTSTATUS Status;
    char Buffer[512];  // �α� �޽����� ������ ����
    LARGE_INTEGER SystemTime, LocalTime;
    TIME_FIELDS TimeFields;

    // ���� �ý��� �ð� ��������
    KeQuerySystemTime(&SystemTime);
    ExSystemTimeToLocalTime(&SystemTime, &LocalTime);
    RtlTimeToTimeFields(&LocalTime, &TimeFields);

    // �α� �޽��� ������ (RtlStringCbPrintfA ���)
    size_t Offset = 0;
    Status = RtlStringCbPrintfA(Buffer, sizeof(Buffer),
        "[%04d-%02d-%02d %02d:%02d:%02d] ",
        TimeFields.Year, TimeFields.Month, TimeFields.Day,
        TimeFields.Hour, TimeFields.Minute, TimeFields.Second);

    if (!NT_SUCCESS(Status)) {
        return; // ������ ���� �� ����
    }
    Offset = strlen(Buffer);

    // ���� ���� ó�� (RtlStringCbVPrintfA ���)
    va_list Args;
    va_start(Args, Format);
    Status = RtlStringCbVPrintfA(Buffer + Offset, sizeof(Buffer) - Offset, Format, Args);
    va_end(Args);

    if (!NT_SUCCESS(Status)) {
        return; // ���ڿ� ���� ���� �� ����
    }

    // ���� ��� ����
    RtlInitUnicodeString(&FilePath, LOG_FILE_PATH);
    InitializeObjectAttributes(&ObjectAttributes, &FilePath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    // ���� ���� (���� ���۸� ���� ���� ����)
    ULONG CreateOptions = FILE_SYNCHRONOUS_IO_NONALERT;
    SIZE_T BytesToWrite = strlen(Buffer);

    // 512����Ʈ ����� �ƴϸ� FILE_NO_INTERMEDIATE_BUFFERING ��� �� ��
    if (BytesToWrite % 512 == 0) {
        CreateOptions |= FILE_NO_INTERMEDIATE_BUFFERING;
    }

    Status = ZwCreateFile(
        &FileHandle,
        FILE_APPEND_DATA | SYNCHRONIZE,  // ���� ���� ����
        &ObjectAttributes,
        &IoStatusBlock,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN_IF,
        CreateOptions,
        NULL,
        0
    );

    if (!NT_SUCCESS(Status)) {
        return; // ������ ���� ���ϸ� ����
    }

    // �α׸� ���Ͽ� ��� ����
    Status = ZwWriteFile(FileHandle, NULL, NULL, NULL, &IoStatusBlock, Buffer, (ULONG)BytesToWrite, NULL, NULL);

    // ����� I/O �÷��� ������� ��� ��ũ �ݿ���

    // ���� �ݱ�
    ZwClose(FileHandle);
}
