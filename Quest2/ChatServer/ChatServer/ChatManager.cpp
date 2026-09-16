#include "ChatManager.h"
#include <algorithm>

ChatManager::ChatManager(std::shared_ptr<OutboundQueue> outbound) : m_outbound(std::move(outbound))
{
    RegisterPacketHandlers();
}

ChatManager::~ChatManager()
{
    RequestStop();
    Join();
}

void ChatManager::Start()
{
    m_thread = std::thread(&ChatManager::Run, this);
}

void ChatManager::Join()
{
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

bool ChatManager::QueueEmpty()
{
    std::lock_guard lock(m_queueMutex);
    return m_queue.empty();
}

bool ChatManager::PushMessageQueue(ServerToManagerPacket packet)
{
    {
        std::lock_guard lock(m_queueMutex);
        if (m_inputClosed && packet.kind == ManagerEventKind::Packet)
        {
            return false;
        }
        m_queue.push(std::move(packet));
    }
    m_condition.notify_one();
    return true;
}

void ChatManager::RequestStop()
{
    // 외부 입력 차단.
    // RoomResult/RoomStopped는 계속 받아야 입장 확정과 자식 종료를 완료할 수 있음.
    // 단순히 큐를 닫고 모든 콜백을 버리면 종료 교착 발생.
    {
        std::lock_guard lock(m_queueMutex);
        if (m_inputClosed)
        {
            return;
        }
        m_inputClosed = true;
        ServerToManagerPacket stop;
        stop.kind = ManagerEventKind::Stop;
        m_queue.push(std::move(stop));
    }
    m_condition.notify_all();
}

void ChatManager::Run()
{
    while (true)
    {
        ServerToManagerPacket packet;
        {
            std::unique_lock lock(m_queueMutex);
#ifdef _DEBUG
            std::cout << "ChatManager : Sleeping... \n";
#endif
            m_condition.wait(lock,
                             [this]
                             {
                                 return !m_queue.empty();
                             });
#ifdef _DEBUG
            std::cout << "ChatManager : Wake... (Sleeping 없으면 Deadlock) \n";
#endif
            packet = std::move(m_queue.front());
            m_queue.pop();
        }
        switch (packet.kind)
        {
            case ManagerEventKind::Packet:
                ManageMessageQueue(std::move(packet));
                break;
            case ManagerEventKind::RoomResult:
                HandleRoomResult(packet.result);
                break;
            case ManagerEventKind::RoomStopped:
                HandleRoomStopped(packet.result.roomID);
                break;
            case ManagerEventKind::Stop:
                m_stopping = true;
                break;
        }
        MaybeStopRooms();
        if (m_stopping && m_rooms.empty() && QueueEmpty())
        {
            break;
        }
    }
    m_done.store(true);
}

void ChatManager::MaybeStopRooms()
{
    if (!m_stopping || m_roomsStopping || !QueueEmpty())
    {
        return;
    }
    for (const auto& [name, user] : m_users)
    {
        if (user.pending)
        {
            return;
        }
    }
    // 미확정 입장 결과/Activate를 마친 뒤 Room의 접수를 닫는다.
    // 그렇지 않으면 성공 응답만 나가고 Activate는 종료 중인 Room에서 거부될 수 있다.
    m_roomsStopping = true;
    for (auto& [id, room] : m_rooms)
    {
        room->RequestStop();
    }
}

void ChatManager::RegisterPacketHandlers()
{
    // 리팩토링 : switch -> PacketCommand -> function handler.
    m_packetHandlers[EPacketType::LoginReq] = [this](auto p)
    {
        MngLoginClient(std::move(p));
    };
    m_packetHandlers[EPacketType::RoomCreateReq] = [this](auto p)
    {
        MngCreateRoom(std::move(p));
    };
    m_packetHandlers[EPacketType::RoomJoinReq] = [this](auto p)
    {
        MngJoinRoom(std::move(p));
    };
    m_packetHandlers[EPacketType::ChattingReq] = [this](auto p)
    {
        MngChatting(std::move(p));
    };
    m_packetHandlers[EPacketType::RoomQuitReq] = [this](auto p)
    {
        MngQuit(std::move(p));
    };
    m_packetHandlers[EPacketType::UserDisconnectedReq] = [this](auto p)
    {
        MngDisconnectUser(std::move(p));
    };
    m_packetHandlers[EPacketType::UserlistReq] = [this](auto p)
    {
        MngUserList(std::move(p));
    };
    m_packetHandlers[EPacketType::RoomlistReq] = [this](auto p)
    {
        MngRoomList(std::move(p));
    };
    m_packetHandlers[EPacketType::WhisperReq] = [this](auto p)
    {
        MngWhisper(std::move(p));
    };
}

void ChatManager::ManageMessageQueue(ServerToManagerPacket packet)
{
    if (auto it = m_packetHandlers.find(packet.type); it != m_packetHandlers.end())
    {
        it->second(std::move(packet));
    }
}

ChatManager::UserData* ChatManager::FindUser(ConnectionId id, std::string& username)
{
    auto mapping = m_connectionToUsername.find(id);
    if (mapping == m_connectionToUsername.end())
    {
        return nullptr;
    }
    username = mapping->second;
    auto user = m_users.find(username);
    return user != m_users.end() && user->second.connectionId == id ? &user->second : nullptr;
}

void ChatManager::MngLoginClient(ServerToManagerPacket packet)
{
    const auto body = std::static_pointer_cast<LoginReqBody>(packet.body);
    const std::string username = body->username;
    LoginResBody response;
    if (username.empty() || m_connectionToUsername.contains(packet.connectionId))
    {
        m_outbound->Send(packet.connectionId, EPacketType::LoginRes, response);
        return;
    }
    auto [it, inserted] = m_users.try_emplace(username);
    auto& user = it->second;
    if (!inserted && (!body->isOldOkay || user.connectionId != INVALID_CONNECTION || user.pending))
    {
        m_outbound->Send(packet.connectionId, EPacketType::LoginRes, response);
        return;
    }
    user.connectionId = packet.connectionId;
    m_connectionToUsername[packet.connectionId] = username;
    if (!inserted && user.joinedRoomID != LOBBY_ID &&
        BeginRoomOperation(user, username, packet, user.joinedRoomID, RoomEventKind::Reconnect))
    {
        return;
    }
    user.joinedRoomID = LOBBY_ID;
    response.isCanLogIn = true;
    response.isNew = inserted;
    m_outbound->Send(packet.connectionId, EPacketType::LoginRes, response);
}

bool ChatManager::BeginRoomOperation(UserData& user, const std::string& username, const ServerToManagerPacket& packet,
                                     UINT roomID, RoomEventKind kind)
{
    auto room = m_rooms.find(roomID);
    if (user.pending || room == m_rooms.end())
    {
        return false;
    }
    // Room 판정을 기다리는 동안 같은 사용자의 중복 입장/퇴장을 거부한다.
    // 결과는 요청의 연결 ID와 대조한다. Disconnect가 먼저 와도 결과는 정리하되 새 연결에 응답하지 않는다.
    user.pending = true;
    user.pendingRoomID = roomID;
    user.pendingConnectionId = packet.connectionId;
    user.pendingOperation = packet.type;
    ManagerToRoomPacket event(packet, username);
    event.kind = kind;
    if (room->second->Post(std::move(event)))
    {
        return true;
    }
    user.pending = false;
    return false;
}

void ChatManager::ReplyOperation(ConnectionId id, EPacketType operation, bool success, UINT roomID)
{
    if (operation == EPacketType::RoomCreateReq)
    {
        CreateRoomResBody body;
        body.isNew = success;
        body.newRoomID = success ? roomID : LOBBY_ID;
        m_outbound->Send(id, EPacketType::RoomCreateRes, body);
    }
    else if (operation == EPacketType::RoomJoinReq)
    {
        JoinRoomResBody body;
        body.isSuccess = success;
        m_outbound->Send(id, EPacketType::RoomJoinRes, body);
    }
    else if (operation == EPacketType::LoginReq)
    {
        LoginResBody body;
        body.isCanLogIn = true; // 기존 방이 없어졌으면 로비로 로그인한다.
        body.roomID = success ? roomID : LOBBY_ID;
        m_outbound->Send(id, EPacketType::LoginRes, body);
    }
}

void ChatManager::HandleRoomResult(const RoomResult& result)
{
    auto it = m_users.find(result.username);
    if (it == m_users.end())
    {
        return;
    }
    auto& user = it->second;
    if (!user.pending || user.pendingConnectionId != result.connectionId || user.pendingRoomID != result.roomID ||
        user.pendingOperation != result.operation)
    {
        return;
    }
    user.pending = false;
    if (result.success)
    {
        user.joinedRoomID = result.operation == EPacketType::RoomQuitReq ? LOBBY_ID : result.roomID;
    }
    else if (result.operation == EPacketType::LoginReq)
    {
        user.joinedRoomID = LOBBY_ID;
    }
    if (user.connectionId != result.connectionId)
    {
        return;
    }
    ReplyOperation(result.connectionId, result.operation, result.success, result.roomID);
    if (result.success && result.operation != EPacketType::RoomQuitReq)
    {
        ManagerToRoomPacket activate;
        activate.kind = RoomEventKind::Activate;
        activate.username = result.username;
        activate.connectionId = result.connectionId;
        if (auto room = m_rooms.find(result.roomID); room != m_rooms.end())
        {
            room->second->Post(std::move(activate));
        }
    }
}

void ChatManager::HandleRoomStopped(UINT roomID)
{
    // 콜백은 이미 반환할 수 있는 비블로킹 enqueue. 어떤 큐 잠금도 잡지 않고 자식 join/삭제.
    m_rooms.erase(roomID);
    for (auto& [name, user] : m_users)
    {
        if (user.joinedRoomID == roomID)
        {
            user.joinedRoomID = LOBBY_ID;
        }
    }
}

void ChatManager::MngCreateRoom(ServerToManagerPacket packet)
{
    std::string username;
    auto* user = FindUser(packet.connectionId, username);
    const auto body = std::static_pointer_cast<CreateRoomReqBody>(packet.body);
    const std::string name = body->roomname;
    bool duplicate = false;
    for (const auto& [id, room] : m_rooms)
    {
        if (room->GetRoomname() == name)
        {
            duplicate = true;
        }
    }
    if (!user || user->pending || user->joinedRoomID != LOBBY_ID || duplicate || name.empty() || m_nextRoomID == 0)
    {
        ReplyOperation(packet.connectionId, packet.type, false, LOBBY_ID);
        return;
    }
    const UINT roomID = m_nextRoomID++;
    auto room = std::make_unique<ChatRoom>(roomID, name, m_outbound,
                                           [this](ServerToManagerPacket result)
                                           {
                                               PushMessageQueue(std::move(result));
                                           });
    m_rooms.emplace(roomID, std::move(room));
    BeginRoomOperation(*user, username, packet, roomID, RoomEventKind::Join);
    m_rooms.at(roomID)->Start();
}

void ChatManager::MngJoinRoom(ServerToManagerPacket packet)
{
    std::string username;
    auto* user = FindUser(packet.connectionId, username);
    const auto body = std::static_pointer_cast<JoinRoomReqBody>(packet.body);
    if (!user || user->joinedRoomID != LOBBY_ID ||
        !BeginRoomOperation(*user, username, packet, body->roomID, RoomEventKind::Join))
    {
        ReplyOperation(packet.connectionId, packet.type, false, LOBBY_ID);
    }
}

void ChatManager::MngChatting(ServerToManagerPacket packet)
{
    std::string username;
    auto* user = FindUser(packet.connectionId, username);
    const auto body = std::static_pointer_cast<ChattingReqBody>(packet.body);
    // 클라이언트가 보낸 roomID만 믿지 않고 Manager의 확정 소속과 대조
    if (!user || user->pending || user->joinedRoomID == LOBBY_ID || user->joinedRoomID != body->roomID)
    {
        return;
    }
    if (auto room = m_rooms.find(user->joinedRoomID); room != m_rooms.end())
    {
        ManagerToRoomPacket event(packet, username);
        event.kind = RoomEventKind::Chat;
        room->second->Post(std::move(event));
    }
}

void ChatManager::MngQuit(ServerToManagerPacket packet)
{
    std::string username;
    auto* user = FindUser(packet.connectionId, username);
    const auto body = std::static_pointer_cast<QuitRoomReqBody>(packet.body);
    if (user && user->joinedRoomID != LOBBY_ID && user->joinedRoomID == body->roomID)
    {
        BeginRoomOperation(*user, username, packet, body->roomID, RoomEventKind::Quit);
    }
}

void ChatManager::MngDisconnectUser(ServerToManagerPacket packet)
{
    std::string username;
    auto* user = FindUser(packet.connectionId, username);
    if (!user)
    {
        return;
    }
    // 입장 결과 전 연결이 끊겨도 예약된 Room으로 Disconnect를 전달해 유령 접속 방지
    const UINT roomID = user->pending ? user->pendingRoomID : user->joinedRoomID;
    user->connectionId = INVALID_CONNECTION;
    m_connectionToUsername.erase(packet.connectionId);
    if (auto room = m_rooms.find(roomID); room != m_rooms.end())
    {
        ManagerToRoomPacket event(packet, username);
        event.kind = RoomEventKind::Disconnect;
        room->second->Post(std::move(event));
    }
}

void ChatManager::MngUserList(ServerToManagerPacket packet)
{
    std::string username;
    if (!FindUser(packet.connectionId, username))
    {
        return;
    }
    UserListResBody body;
    for (const auto& [name, user] : m_users)
    {
        if (body.userCount == MAX_LIST_COUNT)
        {
            break;
        }
        if (user.connectionId != INVALID_CONNECTION)
        {
            strncpy_s(body.users[body.userCount++], name.c_str(), _TRUNCATE);
        }
    }
    m_outbound->Send(packet.connectionId, EPacketType::UserlistRes, body);
}

void ChatManager::MngRoomList(ServerToManagerPacket packet)
{
    std::string username;
    if (!FindUser(packet.connectionId, username))
    {
        return;
    }
    RoomListResBody body;
    for (const auto& [id, room] : m_rooms)
    {
        if (body.roomCount == MAX_LIST_COUNT)
        {
            break;
        }
        body.rooms[body.roomCount++] = id;
    }
    m_outbound->Send(packet.connectionId, EPacketType::RoomlistRes, body);
}

void ChatManager::MngWhisper(ServerToManagerPacket packet)
{
    std::string username;
    if (!FindUser(packet.connectionId, username))
    {
        return;
    }
    const auto request = std::static_pointer_cast<WhisperReqBody>(packet.body);
    if (auto user = m_users.find(request->receivingUser);
        user != m_users.end() && user->second.connectionId != INVALID_CONNECTION)
    {
        WhisperDeliveredResBody body;
        strncpy_s(body.sendingUser, username.c_str(), _TRUNCATE);
        strncpy_s(body.message, request->message, _TRUNCATE);
        if (m_outbound->Send(user->second.connectionId, EPacketType::WhisperDeliveredRes, body))
        {
            return;
        }
    }
    m_outbound->Send(packet.connectionId, EPacketType::WhisperFailRes, WhisperFailedResBody{});
}
