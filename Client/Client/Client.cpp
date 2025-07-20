#include <windows.h>
#include <stdio.h>
#include <vector>

#define SHARED_MEM_SIZE 1024
#define THREAD_COUNT 5

// 전역 변수들
HANDLE hMapFile = NULL;
char* pBuf = NULL;
HANDLE hWriteEvent = NULL;
HANDLE hReadEvent = NULL;
HANDLE hMutex = NULL;

// 스레드 함수
DWORD WINAPI CommunicationThread(LPVOID lpParam)
{
    int threadId = (int)(LPARAM)lpParam;
    
    while (true) {
        Sleep(500);

        // 4. 공유 메모리에 쓰기 전 뮤텍스 잠금 획득 (다른 스레드/프로세스 접근 차단)
        DWORD dwWait = WaitForSingleObject(hMutex, INFINITE);
        if (dwWait != WAIT_OBJECT_0)
        {
            printf("Thread %d: Failed to acquire mutex for writing.\n", threadId);
            break;
        }

        // 임계 구역: 공유 메모리에 데이터 기록
        memset(pBuf, 0, SHARED_MEM_SIZE); // 버퍼 초기화
        sprintf_s(pBuf, SHARED_MEM_SIZE, "Hello, Kernel! thread_id: %d timestamp: %lld", GetCurrentThreadId(), GetTickCount64());

        // 5. 드라이버에 데이터 전송 완료를 알리기 위해 이벤트 신호
        SetEvent(hWriteEvent);

        // 6. 드라이버의 처리가 완료되었는지 기다림
        dwWait = WaitForSingleObject(hReadEvent, INFINITE);
        if (dwWait == WAIT_OBJECT_0)
        {
            // ResetEvent(hReadEvent); 제거 - SynchronizationEvent는 자동 리셋
            printf("Thread %d: Received response from driver: %s\n", threadId, pBuf);
        }
        else
        {
            printf("Thread %d: Timeout waiting for driver response.\n", threadId);
        }

        // 드라이버 응답 완료 후 뮤텍스 해제
        ReleaseMutex(hMutex);
    }
    
    return 0;
}

int main()
{
    // 1. 커널 드라이버가 생성한 공유 메모리를 오픈.
    hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, "Global\\SharedMemorySection");
    if (hMapFile == NULL)
    {
        printf("Could not open file mapping object (%d).\n", GetLastError());
        return 1;
    }
    
    pBuf = (char*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, SHARED_MEM_SIZE);
    if (pBuf == NULL)
    {
        printf("Could not map view of file (%d).\n", GetLastError());
        CloseHandle(hMapFile);
        return 1;
    }
    
    // 2. 드라이버와 동일한 이름으로 생성된 이벤트 오픈
    hWriteEvent = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, "Global\\EventWriteToDriver");
    hReadEvent  = OpenEventA(SYNCHRONIZE, FALSE, "Global\\EventReadFromDriver");
    if (hWriteEvent == NULL || hReadEvent == NULL)
    {
        printf("Could not open events (%d).\n", GetLastError());
        UnmapViewOfFile(pBuf);
        CloseHandle(hMapFile);
        return 1;
    }
    
    // 3. 글로벌 네임드 뮤텍스 오픈 (이름: "Global\SharedMemoryMutex")
    hMutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, "Global\\SharedMemoryMutex");
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
    
    // 5개 스레드 생성
    std::vector<HANDLE> threads(THREAD_COUNT);
    for (int i = 0; i < THREAD_COUNT; i++)
        threads[i] = CreateThread(NULL, 0, CommunicationThread, (LPVOID)i, 0, NULL);
    
    printf("5 threads created successfully. Press Enter to stop all threads...\n");
    system("pause");
    
    // 모든 스레드 강제 종료
    printf("Terminating all threads...\n");
    for (int i = 0; i < THREAD_COUNT; i++)
    {
        TerminateThread(threads[i], 0);
        CloseHandle(threads[i]);
    }
    
    // 7. 정리
    CloseHandle(hMutex);
    CloseHandle(hWriteEvent);
    CloseHandle(hReadEvent);
    UnmapViewOfFile(pBuf);
    CloseHandle(hMapFile);
    
    printf("All threads terminated and resources cleaned up.\n");
    return 0;
}