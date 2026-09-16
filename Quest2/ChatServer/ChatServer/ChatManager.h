#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include "ChatRoom.h"

class ChatManager
{
public:
    explicit ChatManager(std::shared_ptr<OutboundQueue> outbound);
    ~ChatManager();
    void Start();
    void RequestStop();
    void Join();

    bool IsDone() const
    {
        return m_done.load();
    }

    bool PushMessageQueue(ServerToManagerPacket packet);

private:
    void Run();
    void RegisterPacketHandlers();
    void ManageMessageQueue(ServerToManagerPacket packet);
    void MngLoginClient(ServerToManagerPacket packet);
    void MngCreateRoom(ServerToManagerPacket packet);
    void MngJoinRoom(ServerToManagerPacket packet);
    void MngChatting(ServerToManagerPacket packet);
    void MngQuit(ServerToManagerPacket packet);
    void MngDisconnectUser(ServerToManagerPacket packet);
    void MngUserList(ServerToManagerPacket packet);
    void MngRoomList(ServerToManagerPacket packet);
    void MngWhisper(ServerToManagerPacket packet);
    void HandleRoomResult(const RoomResult& result);
    void HandleRoomStopped(UINT roomID);
    void MaybeStopRooms();
    bool QueueEmpty();

    struct UserData
    {
        UINT joinedRoomID = LOBBY_ID;
        ConnectionId connectionId = INVALID_CONNECTION;
        bool pending = false;
        UINT pendingRoomID = LOBBY_ID;
        ConnectionId pendingConnectionId = INVALID_CONNECTION;
        EPacketType pendingOperation = EPacketType::None;
    };

    UserData* FindUser(ConnectionId id, std::string& username);
    bool BeginRoomOperation(UserData& user, const std::string& username, const ServerToManagerPacket& packet,
                            UINT roomID, RoomEventKind kind);
    void ReplyOperation(ConnectionId id, EPacketType operation, bool success, UINT roomID);

    // Manager 상태는 Manager만 변경. Room의 결과 이벤트를 받아 확정.
    // 네트워크/Room은 PushMessageQueue만 호출하며 이 map들을 직접 조회하지 않는다.
    std::unordered_map<ConnectionId, std::string> m_connectionToUsername;
    std::unordered_map<std::string, UserData> m_users;
    std::unordered_map<UINT, std::unique_ptr<ChatRoom>> m_rooms;

    UINT m_nextRoomID = 1; // Manager 전용이므로 atomic 불필요

    std::shared_ptr<OutboundQueue> m_outbound;

    std::thread m_thread;
    std::mutex m_queueMutex;
    std::condition_variable m_condition;

    std::queue<ServerToManagerPacket> m_queue;

    bool m_inputClosed = false; // queue mutex 보호
    bool m_stopping = false;    // Manager 전용
    bool m_roomsStopping = false;
    std::atomic<bool> m_done = false;

    using PacketHandler = std::function<void(ServerToManagerPacket)>;
    std::unordered_map<EPacketType, PacketHandler> m_packetHandlers;
};
