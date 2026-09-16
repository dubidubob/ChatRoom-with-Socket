#include "ChatServer.h"
#include <stdexcept>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace
{
void MakeNonblocking(SOCKET socket)
{
    u_long enabled = 1;
    if (ioctlsocket(socket, FIONBIO, &enabled) == SOCKET_ERROR)
    {
        throw std::runtime_error("ioctlsocket(FIONBIO) failed");
    }
}
} // namespace

ChatServer::ChatServer(ServerConfig config)
    : m_config(config), m_outbound(std::make_shared<OutboundQueue>(config.sendLimits)),
      m_chatManager(std::make_unique<ChatManager>(m_outbound))
{
    if (config.selectTimeoutMs <= 0 || config.drainTimeoutMs < 0)
    {
        throw std::invalid_argument("invalid server timeout");
    }
}

ChatServer::~ChatServer()
{
    // RunServer를 별도 스레드에서 호출한 소유자는 먼저 RequestStop + 그 스레드 join을 해야 한다.
    m_chatManager->RequestStop();
    m_chatManager->Join();
    EndServer();
}

void ChatServer::Init()
{
    if (m_initialized)
    {
        return;
    }
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        throw std::runtime_error("WSAStartup failed");
    }
    m_wsaStarted = true;
    try
    {
        m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listenSock == INVALID_SOCKET)
        {
            throw std::runtime_error("socket failed");
        }
        BOOL exclusive = TRUE;
        if (setsockopt(m_listenSock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive),
                       sizeof(exclusive)) == SOCKET_ERROR)
        {
            throw std::runtime_error("SO_EXCLUSIVEADDRUSE failed");
        }
        MakeNonblocking(m_listenSock);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(m_config.port);
        address.sin_addr.s_addr = INADDR_ANY;
        if (bind(m_listenSock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        {
            throw std::runtime_error("bind failed");
        }
        if (listen(m_listenSock, SOMAXCONN) == SOCKET_ERROR)
        {
            throw std::runtime_error("listen failed");
        }
        int length = sizeof(address);
        if (getsockname(m_listenSock, reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR)
        {
            throw std::runtime_error("getsockname failed");
        }
        m_chatManager->Start();
        m_initialized = true;
        m_boundPort.store(ntohs(address.sin_port));
    }
    catch (...)
    {
        EndServer();
        throw;
    }
}

void ChatServer::RunServer(const std::atomic<bool>* stopSignal)
{
    Init();
    std::cout << "ChatServer ready port=" << BoundPort() << std::endl;
    NetworkLoop(stopSignal);
    m_chatManager->RequestStop();
    m_chatManager->Join();
    EndServer();
    std::cout << "ChatServer stopped; overflow disconnects=" << OverflowCount() << std::endl;
}

void ChatServer::NetworkLoop(const std::atomic<bool>* stopSignal)
{
    bool draining = false;
    bool deadlineStarted = false;
    std::chrono::steady_clock::time_point deadline;
    while (true)
    {
        if (stopSignal && stopSignal->load())
        {
            RequestStop();
        }
        if (m_stopRequested.load() && !draining)
        {
            // detach 무한 루프를 종료 프로토콜로 교체식
            // 입력 중단 > Manager의 요청 처리 > Room 처리/종료 > Manager 종료 > 남은 송신을 제한 시간까지 drain 시킴 >
            // 소켓 종료/join 이후 WSACleanup. Network는 Manager/Room이 송신 요청을 만드는 동안 계속 살아 있어야 한다.
            draining = true;
            closesocket(m_listenSock);
            m_listenSock = INVALID_SOCKET;
            m_chatManager->RequestStop();
        }
        CollectOutbound();
        if (draining && m_chatManager->IsDone())
        {
            if (!deadlineStarted)
            {
                deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_config.drainTimeoutMs);
                deadlineStarted = true;
            }
            if (m_outbound->PendingBytes() == 0)
            {
                break;
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                std::cout << "Send drain deadline reached; pending bytes=" << m_outbound->PendingBytes() << std::endl;
                break; // 수신하지 않는 상대 때문에 서버 종료가 무한 대기하지 않게 하기.
            }
        }

        fd_set reads, writes;
        FD_ZERO(&reads);
        FD_ZERO(&writes);
        if (!draining)
        {
            FD_SET(m_listenSock, &reads);
        }
        for (auto& [id, connection] : m_connections)
        {
            if (!draining)
            {
                FD_SET(connection.socket, &reads);
            }
            if (!connection.session.sendQueue.Empty())
            {
                FD_SET(connection.socket, &writes);
            }
        }
        // condition_variable 통지는 select를 깨우지 않음.
        // 10ms마다 큐와 종료 요청을 확인.
        // TODO : 즉시 wakeup 할라믄 별도 제어 소켓 필요할 듯?
        // writable 소켓을 모두 감시하면 늘 준비된 연결 때문에 busy loop가 발생하므로 대기 송신만 등록.
        if (reads.fd_count == 0 && writes.fd_count == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_config.selectTimeoutMs));
            continue; // Winsock select는 빈 fd_set 조합을 허용하지 않음.
        }
        timeval timeout{m_config.selectTimeoutMs / 1000, (m_config.selectTimeoutMs % 1000) * 1000};
        const int ready = select(0, &reads, &writes, nullptr, &timeout);
        if (ready == SOCKET_ERROR)
        {
            std::cerr << "select failed: " << WSAGetLastError() << std::endl;
            break; // transport 오류는 정상 drain 불가. 아래에서 업무 스레드는 join한다.
        }
        if (!draining && FD_ISSET(m_listenSock, &reads))
        {
            AcceptConnections();
        }
        std::vector<ConnectionId> active;
        for (const auto& [id, connection] : m_connections)
        {
            active.push_back(id);
        }
        for (ConnectionId id : active)
        {
            auto it = m_connections.find(id);
            if (it == m_connections.end())
            {
                continue;
            }
            if (!draining && FD_ISSET(it->second.socket, &reads))
            {
                Receive(id);
            }
            it = m_connections.find(id);
            if (it == m_connections.end())
            {
                continue;
            }
            auto& connection = it->second;
            if (FD_ISSET(connection.socket, &writes))
            {
                const bool ok = connection.session.sendQueue.Flush(
                    [&](const char* data, int size)
                    {
                        return send(connection.socket, data, size, 0);
                    },
                    [&](size_t size)
                    {
                        m_outbound->Complete(id, size);
                    });
                if (!ok)
                {
                    CloseConnection(id);
                }
            }
        }
    }
}

void ChatServer::AcceptConnections()
{
    for (int count = 0; count < 16; ++count) // TODO : 16도 constexpr? 값 검증?
    {
        SOCKET socket = accept(m_listenSock, nullptr, nullptr);
        if (socket == INVALID_SOCKET)
        {
            const int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK)
            {
                std::cerr << "accept failed: " << error << std::endl;
            }
            return;
        }

        if (m_connections.size() >= FD_SETSIZE - 1 || m_nextConnectionId == INVALID_CONNECTION)
        {
            closesocket(socket);
            continue;
        }
        try
        {
            MakeNonblocking(socket);
        }
        catch (...)
        {
            closesocket(socket);
            continue;
        }
        if (m_config.socketSendBuffer > 0)
        {
            setsockopt(socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&m_config.socketSendBuffer),
                       sizeof(int));
        }
        const ConnectionId id = m_nextConnectionId++;
        m_connections.try_emplace(id, socket);
        m_outbound->Register(id);
    }
}

void ChatServer::Receive(ConnectionId id)
{
    auto& connection = m_connections.at(id);
    char buffer[MAX_BUFFER_SIZE];
    const int size = recv(connection.socket, buffer, sizeof(buffer), 0);
    if (size == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
    {
        return;
    }
    if (size <= 0)
    {
        CloseConnection(id);
        return;
    }
    std::vector<ServerToManagerPacket> packets;
    if (connection.session.OnRecv(buffer, size, packets) != AssembleResult::Ok)
    {
        std::cerr << "Invalid packet; connection=" << id << std::endl;
        CloseConnection(id);
        return;
    }
    for (auto& packet : packets)
    {
        packet.connectionId = id;
        m_chatManager->PushMessageQueue(std::move(packet));
    }
}

void ChatServer::CollectOutbound()
{
    std::vector<ConnectionId> closed;
    for (auto& [id, connection] : m_connections)
    {
        std::deque<SharedPacketBytes> packets;
        if (!m_outbound->Take(id, packets))
        {
            closed.push_back(id);
            continue;
        }
        for (auto& packet : packets)
        {
            connection.session.sendQueue.Push(std::move(packet));
        }
    }
    for (ConnectionId id : closed)
    {
        CloseConnection(id);
    }
}

void ChatServer::CloseConnection(ConnectionId id)
{
    auto it = m_connections.find(id);
    if (it == m_connections.end())
    {
        return;
    }
    // close도 Network가 직렬화
    // close/send의 경쟁 및 handle 재사용 안 하기 위해.
    // 등록 해제 뒤 늦게 도착한 ID의 송신 요청은 TrySend에서 거부
    m_outbound->Unregister(id);
    closesocket(it->second.socket);
    m_connections.erase(it);
    m_chatManager->PushMessageQueue(ServerToManagerPacket(id, EPacketType::UserDisconnectedReq));
}

void ChatServer::EndServer()
{
    for (const auto& [id, connection] : m_connections)
    {
        m_outbound->Unregister(id);
        closesocket(connection.socket);
    }
    m_connections.clear();
    if (m_listenSock != INVALID_SOCKET)
    {
        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;
    }
    if (m_wsaStarted)
    {
        WSACleanup();
        m_wsaStarted = false;
    }
}
