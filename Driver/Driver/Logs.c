#include "Logs.h"

void WriteLogToFile(const char* Format, ...) {
    UNICODE_STRING FilePath;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle;
    NTSTATUS Status;
    char Buffer[512];  // 로그 메시지를 저장할 버퍼
    LARGE_INTEGER SystemTime, LocalTime;
    TIME_FIELDS TimeFields;

    // 현재 시스템 시간 가져오기
    KeQuerySystemTime(&SystemTime);
    ExSystemTimeToLocalTime(&SystemTime, &LocalTime);
    RtlTimeToTimeFields(&LocalTime, &TimeFields);

    // 로그 메시지 포맷팅 (RtlStringCbPrintfA 사용)
    size_t Offset = 0;
    Status = RtlStringCbPrintfA(Buffer, sizeof(Buffer),
        "[%04d-%02d-%02d %02d:%02d:%02d] ",
        TimeFields.Year, TimeFields.Month, TimeFields.Day,
        TimeFields.Hour, TimeFields.Minute, TimeFields.Second);

    if (!NT_SUCCESS(Status)) {
        return; // 포맷팅 실패 시 종료
    }
    Offset = strlen(Buffer);

    // 가변 인자 처리 (RtlStringCbVPrintfA 사용)
    va_list Args;
    va_start(Args, Format);
    Status = RtlStringCbVPrintfA(Buffer + Offset, sizeof(Buffer) - Offset, Format, Args);
    va_end(Args);

    if (!NT_SUCCESS(Status)) {
        return; // 문자열 포맷 실패 시 종료
    }

    // 파일 경로 설정
    RtlInitUnicodeString(&FilePath, LOG_FILE_PATH);
    InitializeObjectAttributes(&ObjectAttributes, &FilePath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    // 파일 열기 (파일 버퍼링 해제 조건 수정)
    ULONG CreateOptions = FILE_SYNCHRONOUS_IO_NONALERT;
    SIZE_T BytesToWrite = strlen(Buffer);

    // 512바이트 배수가 아니면 FILE_NO_INTERMEDIATE_BUFFERING 사용 안 함
    if (BytesToWrite % 512 == 0) {
        CreateOptions |= FILE_NO_INTERMEDIATE_BUFFERING;
    }

    Status = ZwCreateFile(
        &FileHandle,
        FILE_APPEND_DATA | SYNCHRONIZE,  // 파일 끝에 쓰기
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
        return; // 파일을 열지 못하면 종료
    }

    // 로그를 파일에 즉시 쓰기
    Status = ZwWriteFile(FileHandle, NULL, NULL, NULL, &IoStatusBlock, Buffer, (ULONG)BytesToWrite, NULL, NULL);

    // 동기식 I/O 플래그 사용으로 즉시 디스크 반영됨

    // 파일 닫기
    ZwClose(FileHandle);
}
