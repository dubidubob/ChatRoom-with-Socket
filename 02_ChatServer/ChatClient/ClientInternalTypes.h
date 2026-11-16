// ClientInternalTypes.h
#pragma once
#include "Protocol.h"

struct ClientPacket
{
    EPacketType type = EPacketType::None;
    std::shared_ptr<IBody> body = nullptr;
};