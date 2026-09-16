#include "Session.h"
#include "Protocol.h"

#include <functional>
#include <unordered_map>

namespace
{
using BodyCreator = std::function<std::shared_ptr<IBody>(const char*, UINT16)>;

template <typename BodyType>
std::shared_ptr<IBody> CreateTypedBody(const char* bodyData, UINT16 bodyLength)
{
    return CreateBody<BodyType>(bodyData, bodyLength);
}

const std::unordered_map<EPacketType, BodyCreator>& GetServerBodyCreators()
{
    static const std::unordered_map<EPacketType, BodyCreator> bodyCreators = {
        {EPacketType::LoginReq, CreateTypedBody<LoginReqBody>},
        {EPacketType::RoomCreateReq, CreateTypedBody<CreateRoomReqBody>},
        {EPacketType::RoomJoinReq, CreateTypedBody<JoinRoomReqBody>},
        {EPacketType::ChattingReq, CreateTypedBody<ChattingReqBody>},
        {EPacketType::RoomQuitReq, CreateTypedBody<QuitRoomReqBody>},
        {EPacketType::WhisperReq, CreateTypedBody<WhisperReqBody>},
        {EPacketType::UserlistReq, CreateTypedBody<UserListReqBody>},
        {EPacketType::RoomlistReq, CreateTypedBody<RoomListReqBody>},
    };

    return bodyCreators;
}
}

AssembleResult Session::OnRecv(const char* buffer, int len, std::vector<ServerToManagerPacket>& packets)
{
    return m_assembler.ProcessBytes(buffer, len, packets);
}

std::shared_ptr<IBody> Session::CreateServerBody(EPacketType type, const char* bodyData, UINT16 bodyLength)
{

    const auto& bodyCreators = GetServerBodyCreators();
    auto it = bodyCreators.find(type);
    if (it == bodyCreators.end())
    {
        std::cout << "Session: unknown packet type: " << static_cast<int>(type) << std::endl;
        return nullptr;
    }

    return it->second(bodyData, bodyLength);
}
