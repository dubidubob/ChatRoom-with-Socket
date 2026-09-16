#pragma once
#include <functional>
#include <memory>
#include <string>
#include "Protocol.h"
#include "OutboundQueue.h"

// raw SOCKET은 closesocket 후 다른 접속에 재사용됨
// 지연된 송신/방 이벤트가 새 접속에 적용되지 않도록 프로세스 내 단조 증가 ID를 사용.
// SOCKET은 Network/Session에만 남김.
enum class ManagerEventKind
{
    Packet,
    RoomResult,
    RoomStopped,
    Stop
};
enum class RoomEventKind
{
    Join,
    Chat,
    Quit,
    Disconnect,
    Reconnect,
    Activate
};

struct RoomResult
{
    UINT roomID = LOBBY_ID;
    std::string username;
    ConnectionId connectionId = INVALID_CONNECTION;
    EPacketType operation = EPacketType::None;
    bool success = false;
};

struct ServerToManagerPacket
{
    ConnectionId connectionId = INVALID_CONNECTION;
    EPacketType type = EPacketType::None;
    std::shared_ptr<IBody> body;
    ManagerEventKind kind = ManagerEventKind::Packet;
    RoomResult result;
    ServerToManagerPacket() = default;

    explicit ServerToManagerPacket(EPacketType inType) : type(inType)
    {
    }

    ServerToManagerPacket(ConnectionId id, EPacketType inType) : connectionId(id), type(inType)
    {
    }
};

struct ManagerToRoomPacket
{
    RoomEventKind kind = RoomEventKind::Chat;
    std::string username;
    ConnectionId connectionId = INVALID_CONNECTION;
    EPacketType type = EPacketType::None;
    std::shared_ptr<IBody> body;
    ManagerToRoomPacket() = default;

    ManagerToRoomPacket(const ServerToManagerPacket& packet, std::string name)
        : username(std::move(name)), connectionId(packet.connectionId), type(packet.type), body(packet.body)
    {
    }
};

using RoomEventCallback = std::function<void(ServerToManagerPacket)>;
