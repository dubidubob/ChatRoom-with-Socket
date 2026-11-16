// serverMain.cpp
#include <iostream>

#include "Protocol.h"
#include "ChatClient.h"

int main()
{
    ChatClient client;
    if (client.ConnectServer(IP, PORTNUM))
    {
        client.Run();
    }
    else
    {
        std::cout << "서버 연결에 실패했습니다." << std::endl;
    }

    return 0;
}