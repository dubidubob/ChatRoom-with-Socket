#include "ChatRoom.h"

ChatRoom::~ChatRoom()
{
    RequestStop();
    Join();
}

void ChatRoom::Start()
{
    m_thread = std::thread(&ChatRoom::Run, this);
}

void ChatRoom::Join()
{
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

bool ChatRoom::Post(ManagerToRoomPacket packet)
{
    {
        std::lock_guard lock(m_queueMutex);
        if (!m_accepting)
        {
            return false;
        }
        m_queue.push(std::move(packet));
    }
    m_condition.notify_one();
    return true;
}

void ChatRoom::RequestStop()
{
    // notify만으로는 predicate가 false인 대기를 끝낼 수 없다.
    // 같은 mutex로 접수 차단/종료 상태를 설정하고 깨우기, 이미 접수된 이벤트는 모두 소비하기.
    {
        std::lock_guard lock(m_queueMutex);
        m_accepting = false;
        m_stopping = true;
    }
    m_condition.notify_all();
}

void ChatRoom::Run()
{
    while (true)
    {
        ManagerToRoomPacket packet;
        {
            std::unique_lock lock(m_queueMutex);
            m_condition.wait(lock,
                             [this]
                             {
                                 return m_stopping || !m_queue.empty();
                             });
            if (m_queue.empty() && m_stopping)
            {
                break;
            }
            packet = std::move(m_queue.front());
            m_queue.pop();
        }
        Process(std::move(packet));
        {
            std::lock_guard lock(m_queueMutex);
            // 빈 방 종료 결정과 Post의 접수 판정을 같은 잠금으로 묶는다.
            // 마지막 퇴장 직후 이미 접수된 Join을 버리거나, 종료할 방에 Join을 접수하지 않음.
            if (m_users.empty() && m_queue.empty())
            {
                m_accepting = false;
                m_stopping = true;
            }
        }
    }
    // Room은 자신을 삭제하지 않음. 콜백은 큐에만 넣고 즉시 반환.
    // Manager가 통지를 소비하고 join/삭제한다 > Manager 객체/큐는 모든 Room보다 오래 살아야함.
    ServerToManagerPacket stopped;
    stopped.kind = ManagerEventKind::RoomStopped;
    stopped.result.roomID = m_roomID;
    m_callback(std::move(stopped));
}

void ChatRoom::Report(const ManagerToRoomPacket& packet, bool success)
{
    ServerToManagerPacket result;
    result.kind = ManagerEventKind::RoomResult;
    result.result = {m_roomID, packet.username, packet.connectionId, packet.type, success};
    m_callback(std::move(result));
}

void ChatRoom::Process(ManagerToRoomPacket packet)
{
    auto user = m_users.find(packet.username);
    switch (packet.kind)
    {
        case RoomEventKind::Join:
        {
            // IsRoomFull 조회 후 나중에 추가하던 구조는 race condition 유발, 제거했음.
            // 정원 검사 + 실제 삽입은 Room의 한 이벤트에서 실행하고 결과만 Manager로 보낸다.
            const bool success = user == m_users.end() && m_users.size() < MAX_ROOM_USER;
            if (success)
            {
                m_users.emplace(packet.username, UserRoomData{packet.connectionId});
            }
            Report(packet, success);
            break;
        }
        case RoomEventKind::Reconnect:
        {
            const bool success = user != m_users.end() && user->second.connectionId == INVALID_CONNECTION;
            if (success)
            {
                user->second.connectionId = packet.connectionId;
                user->second.needsReplay = true;
                user->second.isLive = false;
            }
            Report(packet, success);
            break;
        }
        case RoomEventKind::Activate:
            if (user != m_users.end() && user->second.connectionId == packet.connectionId && !user->second.isLive)
            {
                // Manager가 성공 응답을 송신 큐에 넣은 뒤 Activate를 보낸다.
                // 재접속 기록을 먼저 enqueue > 실시간 수신자로 전환한다. 같은 Room의 과거/실시간 순서를 유지
                const bool replay = user->second.needsReplay;
                if (replay)
                {
                    SendUnsendedMessages(packet.connectionId, user->second.lastReadIndex);
                }
                user->second.needsReplay = false;
                user->second.isLive = true;
                if (!replay)
                {
                    BroadcastPacket(packet.username, "님이 들어왔습니다.");
                }
            }
            break;
        case RoomEventKind::Disconnect:
            if (user != m_users.end() && user->second.connectionId == packet.connectionId)
            {
                user->second.connectionId = INVALID_CONNECTION;
                user->second.isLive = false;
                user->second.lastReadIndex = m_messageCounter;
            }
            break;
        case RoomEventKind::Quit:
        {
            const bool success = user != m_users.end() && user->second.connectionId == packet.connectionId;
            if (success)
            {
                m_users.erase(user);
                BroadcastPacket(packet.username, "님이 나가셨습니다.");
            }
            Report(packet, success);
            break;
        }
        case RoomEventKind::Chat:
            if (user != m_users.end() && user->second.connectionId == packet.connectionId && user->second.isLive)
            {
                auto body = std::static_pointer_cast<ChattingReqBody>(packet.body);
                BroadcastPacket(packet.username, body->message);
            }
            break;
    }
}

void ChatRoom::BroadcastPacket(const std::string& username, const std::string& message)
{
    RoomMessage entry{++m_messageCounter, username, message};
    if (m_messages.size() < MAX_SAVED_MESSAGE_COUNT)
    {
        m_messages.push_back(std::move(entry));
    }
    else
    {
        m_messages[m_endIndex] = std::move(entry);
    }
    m_endIndex = (m_endIndex + 1) % MAX_SAVED_MESSAGE_COUNT;
    ChattingBroadcastBody body;
    strncpy_s(body.sendingUser, username.c_str(), _TRUNCATE);
    strncpy_s(body.message, message.c_str(), _TRUNCATE);
    auto bytes = SerializePacket(EPacketType::ChattingBroadcast, body);
    // 바이트 공유: 소켓 I/O나 큐 공간을 기다리지 않음.
    // TrySend 실패 시 OutboundQueue가 해당 연결을 종료 대상으로 표시/이미 종료로 판정한다.
    for (const auto& [name, user] : m_users)
    {
        if (user.isLive)
        {
            m_outbound->TrySend(user.connectionId, bytes);
        }
    }
}

void ChatRoom::SendUnsendedMessages(ConnectionId id, UINT64 lastReadIndex)
{
    const size_t count = m_messages.size();
    const size_t start = count == MAX_SAVED_MESSAGE_COUNT ? m_endIndex : 0;
    for (size_t i = 0; i < count; ++i)
    {
        const auto& message = m_messages[(start + i) % count];
        if (message.index <= lastReadIndex)
        {
            continue;
        }
        ChattingBroadcastBody body;
        strncpy_s(body.sendingUser, message.username.c_str(), _TRUNCATE);
        strncpy_s(body.message, message.message.c_str(), _TRUNCATE);
        if (!m_outbound->Send(id, EPacketType::ChattingBroadcast, body))
        {
            break;
        }
    }
}
