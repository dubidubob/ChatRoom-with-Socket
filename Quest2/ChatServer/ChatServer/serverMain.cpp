#include "ChatServer.h"
#include <Windows.h>

namespace
{
std::atomic<bool> stopRequested = false;

BOOL WINAPI HandleConsoleControl(DWORD event)
{
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT)
    {
        // 콘솔 콜백에서 join/소켓 종료를 하지 않고 요청만 남긴다.
        // 프로세스가 살아 있는 동안 유지되는 flag라 서버 객체의 수명과 경쟁하지 않는다.
        stopRequested.store(true);
        return TRUE;
    }
    return FALSE;
}
} // namespace

int main()
{
    SetConsoleCtrlHandler(HandleConsoleControl, TRUE);
    try
    {
        ChatServer server;
        server.RunServer(&stopRequested);
    }
    catch (const std::exception& error)
    {
        std::cerr << "ChatServer: " << error.what() << std::endl;
        return 1;
    }
    SetConsoleCtrlHandler(HandleConsoleControl, FALSE);
    return 0;
}
