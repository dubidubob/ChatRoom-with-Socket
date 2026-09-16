#pragma once
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include "ServerInternalTypes.h"

class ChatRoom
{
public:
    ChatRoom(UINT id, std::string name, std::shared_ptr<OutboundQueue> outbound, RoomEventCallback callback)
        : m_roomID(id), m_roomname(std::move(name)), m_outbound(std::move(outbound)), m_callback(std::move(callback))
    {
    }

    ~ChatRoom();
    void Start();
    bool Post(ManagerToRoomPacket packet);
    void RequestStop();
    void Join();

    const std::string& GetRoomname() const
    {
        return m_roomname;
    } // 생성 후 불변
private:
    void Run();
    void Process(ManagerToRoomPacket packet);
    void Report(const ManagerToRoomPacket& packet, bool success);
    void BroadcastPacket(const std::string& username, const std::string& message);
    void SendUnsendedMessages(ConnectionId id, UINT64 lastReadIndex);

    struct RoomMessage
    {
        UINT64 index;
        std::string username;
        std::string message;
    };

    struct UserRoomData
    {
        ConnectionId connectionId = INVALID_CONNECTION;
        bool isLive = false;
        bool needsReplay = false;
        UINT64 lastReadIndex = 0;
    };

    const UINT m_roomID;
    const std::string m_roomname;
    std::shared_ptr<OutboundQueue> m_outbound;
    RoomEventCallback m_callback;
    std::thread m_thread;
    std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::queue<ManagerToRoomPacket> m_queue;
    bool m_accepting = true; // queue mutex 보호
    bool m_stopping = false;

    // 기존 Manager가 Disconnect/Reconnect/IsRoomFull로 Room 자료에 직접 접근 > shared_mutex가 필요 > send 중 잠금이 Manager 지연
    // 리팩토링 : Room 이벤트로 통일, 아래 상태는 Room 스레드에서만 접근하므로 잠금 불필요.
    std::unordered_map<std::string, UserRoomData> m_users;
    std::vector<RoomMessage> m_messages;
    size_t m_endIndex = 0;
    UINT64 m_messageCounter = 0;
};
