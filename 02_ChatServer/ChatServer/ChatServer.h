#pragma once
#include <atomic>
#include <chrono>
#include <unordered_map>
#include "ChatManager.h"
#include "Session.h"

struct ServerConfig
{
    unsigned short port = PORTNUM;
    SendLimits sendLimits;
    int selectTimeoutMs = 10; // timeout 기반 wakeup
    int drainTimeoutMs = 2000;
    int socketSendBuffer = 0; // 0은 OS 기본값.
};

class ChatServer
{
public:
    explicit ChatServer(ServerConfig config = {});
    ~ChatServer();

    void Init();
    void RunServer(const std::atomic<bool>* stopSignal = nullptr);

    void RequestStop()
    {
        m_stopRequested.store(true);
    }

    unsigned short BoundPort() const
    {
        return m_boundPort.load();
    }

    size_t OverflowCount() const
    {
        return m_outbound->OverflowCount();
    }

private:
    void NetworkLoop(const std::atomic<bool>* stopSignal);
    void AcceptConnections();
    void Receive(ConnectionId id);
    void CollectOutbound();
    void CloseConnection(ConnectionId id);
    void EndServer();

    struct NetworkConnection
    {
        explicit NetworkConnection(SOCKET value) : socket(value)
        {
        }

        SOCKET socket;
        Session session;
    };

    ServerConfig m_config;

    std::shared_ptr<OutboundQueue> m_outbound;
    std::unique_ptr<ChatManager> m_chatManager;

    SOCKET m_listenSock = INVALID_SOCKET;

    std::unordered_map<ConnectionId, NetworkConnection> m_connections;
    ConnectionId m_nextConnectionId = 1;

    std::atomic<bool> m_stopRequested = false;
    std::atomic<unsigned short> m_boundPort = 0;
    bool m_wsaStarted = false;
    bool m_initialized = false;
};
