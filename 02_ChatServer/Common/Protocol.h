// protocol.h
#pragma once
#include <cstdint>
#include <iostream>

// Winsock 기본 FD_SETSIZE(64)는 select 동시 접속을 63으로 제한한다.
// WinSock2.h보다 먼저 정의되어야 하므로 Winsock 포함은 이 헤더에서만 한다.
#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <string_view>
#include <cstring>
#include <memory>
#include <vector>
#include <type_traits>
#include <limits>

#if defined(_MSC_VER)
#define PRETTY_FUNCTION_MACRO __FUNCSIG__
#elif defined(__GNUC__) || defined(__clang__)
#define PRETTY_FUNCTION_MACRO __PRETTY_FUNCTION__
#else
#define PRETTY_FUNCTION_MACRO __func__ // Fallback
#endif

// 클라이언트-서버 프로토콜. 바디 구조체를 그대로 memcpy하므로 문자열은 고정 길이.
constexpr int MAX_USERNAME_LEN = 32; // NUL 포함
constexpr int MAX_ROOM_TITLE_LEN = 32;
constexpr int MAX_MESSAGE_LEN = 128;
constexpr int MAX_SAVED_MESSAGE_COUNT = 128;

constexpr int LOBBY_ID = 0;

constexpr size_t MAX_PACKET_BODY_LENGTH = 65535; // UINT16 wire length의 표현 범위
constexpr UINT MAX_BUFFER_SIZE = 4096;           // recv 한 번의 크기. 가장 큰 패킷(UserListRes, 약 1.6KB)보다 커야 한다.
constexpr UINT16 MAX_LIST_COUNT = 50;
constexpr UINT16 MAX_ROOM_USER = 10;
constexpr u_short PORTNUM = 8080;
constexpr char IP[] = "127.0.0.1";

enum class EPacketType : uint8_t
{
    LoginReq,
    LoginRes,

    RoomCreateReq,
    RoomCreateRes,

    RoomJoinReq,
    RoomJoinRes,

    RoomQuitReq,
    UserDisconnectedReq,

    UserlistReq,
    UserlistRes,

    RoomlistReq,
    RoomlistRes,

    ChattingReq,
    ChattingBroadcast,
    ChattingPended,
    ChattingConfirm,

    WhisperReq,
    WhisperFailRes,
    WhisperDeliveredRes,

    RoomDeleteInternal,

    None,
};

// Structs================================================

#pragma pack(push, 1)

struct PacketHeader
{
    UINT16 bodyLength = 0;
    EPacketType type = EPacketType::None;
};

struct IBody
{
    // 가상 소멸자 없음: 바디는 trivially copyable이어야 하고(memcpy 직렬화),
    // 항상 make_shared<구체타입>으로 생성되어 shared_ptr deleter가 실제 타입을 소멸시킨다.
    // IBody*를 직접 delete하지 말 것.
};

struct LoginReqBody : public IBody
{
    char username[MAX_USERNAME_LEN] = {0};
    bool isOldOkay = false; // true: 이미 있는 이름이면 그 계정으로 재접속하겠다는 확인
};

struct LoginResBody : public IBody
{
    bool isCanLogIn = false;
    bool isNew = false;
    UINT roomID = LOBBY_ID;
};

struct CreateRoomReqBody : public IBody
{
    char roomname[MAX_ROOM_TITLE_LEN] = {0};
};

struct CreateRoomResBody : public IBody
{
    bool isNew = false;
    UINT newRoomID = LOBBY_ID;
};

struct JoinRoomReqBody : public IBody
{
    UINT roomID = LOBBY_ID;
};

struct JoinRoomResBody : public IBody
{
    bool isSuccess = false;
};

struct ChattingReqBody : public IBody
{
    UINT roomID = LOBBY_ID;
    char message[MAX_MESSAGE_LEN] = {0};
};

struct ChattingBroadcastBody : public IBody
{
    char sendingUser[MAX_USERNAME_LEN] = {0};
    char message[MAX_MESSAGE_LEN] = {0};
};

struct ChattingConfirmReqBody : public IBody
{
    UINT roomID = LOBBY_ID;
    UINT lastReadIndex = 0;
};

struct QuitRoomReqBody : public IBody
{
    UINT roomID = LOBBY_ID;
};

struct WhisperReqBody : public IBody
{
    char receivingUser[MAX_USERNAME_LEN] = {0};
    char message[MAX_MESSAGE_LEN] = {0};
};

struct WhisperFailedResBody : public IBody
{
};

struct WhisperDeliveredResBody : public IBody
{
    char sendingUser[MAX_USERNAME_LEN] = {0};
    char message[MAX_MESSAGE_LEN] = {0};
};

struct UserListReqBody : public IBody
{
};

struct UserListResBody : public IBody
{
    UINT16 userCount = 0;
    char users[MAX_LIST_COUNT][MAX_USERNAME_LEN] = {0};
};

struct RoomListReqBody : public IBody
{
};

struct RoomListResBody : public IBody
{
    UINT16 roomCount = 0;
    UINT rooms[MAX_LIST_COUNT] = {0};
};

struct RoomDeleteInternalBody : public IBody
{
    UINT roomID = LOBBY_ID;
};

#pragma pack(pop)

// Helpers================================================s
// enum에는 내부 이벤트도 섞여 있었다. 수신 방향별 허용
// 타입/길이를 검증해 클라이언트가 UserDisconnectedReq/RoomDeleteInternal을 주입하지 못하게 한다.
inline size_t ExpectedBodySize(EPacketType type, bool serverReceiving)
{
    if (serverReceiving)
    {
        switch (type)
        {
            case EPacketType::LoginReq:
                return sizeof(LoginReqBody);
            case EPacketType::RoomCreateReq:
                return sizeof(CreateRoomReqBody);
            case EPacketType::RoomJoinReq:
                return sizeof(JoinRoomReqBody);
            case EPacketType::RoomQuitReq:
                return sizeof(QuitRoomReqBody);
            case EPacketType::ChattingReq:
                return sizeof(ChattingReqBody);
            case EPacketType::WhisperReq:
                return sizeof(WhisperReqBody);
            case EPacketType::UserlistReq:
                return sizeof(UserListReqBody);
            case EPacketType::RoomlistReq:
                return sizeof(RoomListReqBody);
            default:
                return 0;
        }
    }
    switch (type)
    {
        case EPacketType::LoginRes:
            return sizeof(LoginResBody);
        case EPacketType::RoomCreateRes:
            return sizeof(CreateRoomResBody);
        case EPacketType::RoomJoinRes:
            return sizeof(JoinRoomResBody);
        case EPacketType::ChattingBroadcast:
        case EPacketType::ChattingPended:
            return sizeof(ChattingBroadcastBody);
        case EPacketType::WhisperFailRes:
            return sizeof(WhisperFailedResBody);
        case EPacketType::WhisperDeliveredRes:
            return sizeof(WhisperDeliveredResBody);
        case EPacketType::UserlistRes:
            return sizeof(UserListResBody);
        case EPacketType::RoomlistRes:
            return sizeof(RoomListResBody);
        default:
            return 0;
    }
}

inline bool ValidateHeader(const PacketHeader& header, bool serverReceiving)
{
    const size_t expected = ExpectedBodySize(header.type, serverReceiving);
    return expected != 0 && header.bodyLength == expected;
}

using ByteBuffer = std::vector<char>;
using SharedPacketBytes = std::shared_ptr<const ByteBuffer>;

// 서버 로직은 불변 바이트만 만들고 send()는 Network 스레드만 한다.
// 브로드캐스트는 같은 바이트를 공유하고 전송 offset만 연결별로 둔다.
template <typename Body>
SharedPacketBytes SerializePacket(EPacketType type, const Body& body)
{
    static_assert(std::is_trivially_copyable_v<Body>);
    static_assert(sizeof(Body) <= MAX_PACKET_BODY_LENGTH);
    auto bytes = std::make_shared<ByteBuffer>(sizeof(PacketHeader) + sizeof(Body));
    PacketHeader header{static_cast<UINT16>(sizeof(Body)), type};
    std::memcpy(bytes->data(), &header, sizeof(header));
    std::memcpy(bytes->data() + sizeof(header), &body, sizeof(body));
    return bytes;
}

template <typename ConcreteIBody>
bool SendPacket(SOCKET sock, PacketHeader header, const ConcreteIBody& body)
{
    const int totalLength = sizeof(PacketHeader) + sizeof(ConcreteIBody);
    char sendBuffer[totalLength];

    memcpy(sendBuffer, &header, sizeof(PacketHeader));
    memcpy(sendBuffer + sizeof(PacketHeader), &body, sizeof(ConcreteIBody));
    // 콘솔 클라이언트의 단일 송신 스레드용 blocking helper만 유지.
    //  서버는 이 함수를 사용하지 않고 Session의 nonblocking 송신 큐를 사용.
    int offset = 0;
    while (offset < totalLength)
    {
        const int sent = send(sock, sendBuffer + offset, totalLength - offset, 0);
        if (sent <= 0)
        {
            return false;
        }
        offset += sent;
    }
    return true;
}

template <typename ConcreteIBody>
bool SendPacket(SOCKET sock, EPacketType messageType, const ConcreteIBody& body)
{
    PacketHeader header;
    header.type = messageType;
    header.bodyLength = static_cast<UINT16>(sizeof(ConcreteIBody));

    return SendPacket(sock, header, body);
}

template <typename ConcreteIBody>
std::shared_ptr<IBody> CreateBody(const char* rawData, UINT16 bodyLength)
{
    if (bodyLength != sizeof(ConcreteIBody))
    {
        std::cout << "ChatManager : " << PRETTY_FUNCTION_MACRO << ": Packet Length 불일치\n";
        return nullptr;
    }

    // 바이트를 객체 포인터로 reinterpret_cast해 역참조하지 않음.
    // 먼저 실제 객체를 생성한 뒤 memcpy.
    static_assert(std::is_trivially_copyable_v<ConcreteIBody>);
    static_assert(std::is_standard_layout_v<ConcreteIBody>);
    if (!rawData)
    {
        return nullptr;
    }
    auto validBool = [&](size_t offset)
    {
        return static_cast<unsigned char>(rawData[offset]) <= 1;
    };
    if constexpr (std::is_same_v<ConcreteIBody, LoginReqBody>)
    {
        if (!validBool(offsetof(LoginReqBody, isOldOkay)))
        {
            return nullptr;
        }
    }
    if constexpr (std::is_same_v<ConcreteIBody, LoginResBody>)
    {
        if (!validBool(offsetof(LoginResBody, isCanLogIn)) || !validBool(offsetof(LoginResBody, isNew)))
        {
            return nullptr;
        }
    }
    if constexpr (std::is_same_v<ConcreteIBody, CreateRoomResBody>)
    {
        if (!validBool(offsetof(CreateRoomResBody, isNew)))
        {
            return nullptr;
        }
    }
    if constexpr (std::is_same_v<ConcreteIBody, JoinRoomResBody>)
    {
        if (!validBool(offsetof(JoinRoomResBody, isSuccess)))
        {
            return nullptr;
        }
    }
    auto body = std::make_shared<ConcreteIBody>();
    std::memcpy(body.get(), rawData, sizeof(ConcreteIBody));
    // 길이가 맞아도 NUL 없는 문자열/잘못된 목록 개수는 후속 처리에서 범위 밖을 읽게 만든다.
    if constexpr (requires { body->username; })
    {
        if (!std::memchr(body->username, 0, sizeof(body->username)))
        {
            return nullptr;
        }
    }
    if constexpr (requires { body->roomname; })
    {
        if (!std::memchr(body->roomname, 0, sizeof(body->roomname)))
        {
            return nullptr;
        }
    }
    if constexpr (requires { body->message; })
    {
        if (!std::memchr(body->message, 0, sizeof(body->message)))
        {
            return nullptr;
        }
    }
    if constexpr (requires { body->sendingUser; })
    {
        if (!std::memchr(body->sendingUser, 0, sizeof(body->sendingUser)))
        {
            return nullptr;
        }
    }
    if constexpr (requires { body->receivingUser; })
    {
        if (!std::memchr(body->receivingUser, 0, sizeof(body->receivingUser)))
        {
            return nullptr;
        }
    }
    if constexpr (std::is_same_v<ConcreteIBody, UserListResBody>)
    {
        if (body->userCount > MAX_LIST_COUNT)
        {
            return nullptr;
        }
        for (UINT16 i = 0; i < body->userCount; ++i)
        {
            if (!std::memchr(body->users[i], 0, MAX_USERNAME_LEN))
            {
                return nullptr;
            }
        }
    }
    if constexpr (std::is_same_v<ConcreteIBody, RoomListResBody>)
    {
        if (body->roomCount > MAX_LIST_COUNT)
        {
            return nullptr;
        }
    }
    return body;
}
