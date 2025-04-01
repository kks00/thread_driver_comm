#include <windows.h>
#include <stdio.h>

#define SHARED_MEM_SIZE 1024

int main()
{
    // 1. 커널 드라이버가 생성한 공유 메모리를 오픈.
    HANDLE hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, "Global\\SharedMemorySection");
    if (hMapFile == NULL)
    {
        printf("Could not open file mapping object (%d).\n", GetLastError());
        return 1;
    }
    
    char* pBuf = (char*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, SHARED_MEM_SIZE);
    if (pBuf == NULL)
    {
        printf("Could not map view of file (%d).\n", GetLastError());
        CloseHandle(hMapFile);
        return 1;
    }
    
    // 2. 드라이버와 동일한 이름으로 생성된 이벤트 오픈
    HANDLE hWriteEvent = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, "Global\\EventWriteToDriver");
    HANDLE hReadEvent  = OpenEventA(SYNCHRONIZE, FALSE, "Global\\EventReadFromDriver");
    if (hWriteEvent == NULL || hReadEvent == NULL)
    {
        printf("Could not open events (%d).\n", GetLastError());
        UnmapViewOfFile(pBuf);
        CloseHandle(hMapFile);
        return 1;
    }
    
    // 3. 글로벌 네임드 뮤텍스 오픈 (이름: "Global\SharedMemoryMutex")
    HANDLE hMutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, "Global\\SharedMemoryMutex");
    if (hMutex == NULL)
    {
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
        {
            // mutex가 없으므로 현재 프로세스에서 생성합니다.
            hMutex = CreateMutexA(NULL, FALSE, "Global\\SharedMemoryMutex");
            if (hMutex == NULL)
            {
                printf("Could not create mutex (%d).\n", GetLastError());
                UnmapViewOfFile(pBuf);
                CloseHandle(hMapFile);
                return 1;
            }
            else
            {
                printf("Mutex created by client.\n");
            }
        }
        else
        {
            printf("Could not open mutex (%d).\n", GetLastError());
            UnmapViewOfFile(pBuf);
            CloseHandle(hMapFile);
            return 1;
        }
    }
    
    // 4. 공유 메모리에 쓰기 전 뮤텍스 잠금 획득 (다른 스레드/프로세스 접근 차단)
    DWORD dwWait = WaitForSingleObject(hMutex, INFINITE);
    if (dwWait != WAIT_OBJECT_0)
    {
        printf("Failed to acquire mutex for writing.\n");
        CloseHandle(hMutex);
        UnmapViewOfFile(pBuf);
        CloseHandle(hMapFile);
        return 1;
    }
    
    // 임계 구역: 공유 메모리에 데이터 기록
    sprintf_s(pBuf, SHARED_MEM_SIZE, "Hello, Kernel!");
    
    // 5. 드라이버에 데이터 전송 완료를 알리기 위해 이벤트 신호
    SetEvent(hWriteEvent);
    
    // 6. 드라이버의 처리가 완료되었는지 기다림
    dwWait = WaitForSingleObject(hReadEvent, INFINITE);
    if (dwWait == WAIT_OBJECT_0)
    {
        ResetEvent(hReadEvent);
        printf("Received response from driver: %s\n", pBuf);
    }
    else
    {
        printf("Timeout waiting for driver response.\n");
    }

    // 드라이버 응답 완료 후 뮤텍스 해제
    ReleaseMutex(hMutex);
    
    // 7. 정리
    CloseHandle(hMutex);
    CloseHandle(hWriteEvent);
    CloseHandle(hReadEvent);
    UnmapViewOfFile(pBuf);
    CloseHandle(hMapFile);
    
    return 0;
}