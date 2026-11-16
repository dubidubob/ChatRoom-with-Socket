#pragma once
#include <deque>
#include "ServerInternalTypes.h"
#include "PacketAssembler.h"

// 부분 송신 상태를 패킷별로 보존 > 오직 Network가 접근
// bytes는 송신이 끝날 때까지 살아 있고 offset은 연결마다 독립적이다.
class SendBufferQueue
{
public:
    void Push(SharedPacketBytes bytes)
    {
        m_pending.push_back({std::move(bytes), 0});
    }

    bool Empty() const
    {
        return m_pending.empty();
    }

    template <typename SendFunction, typename CompleteFunction>
    bool Flush(SendFunction&& sendFunction, CompleteFunction&& complete, size_t budget = 64 * 1024)
    {
        while (!m_pending.empty() && budget > 0)
        {
            auto& pending = m_pending.front();
            const size_t count = (std::min)(pending.bytes->size() - pending.offset, budget);
            const int sent = sendFunction(pending.bytes->data() + pending.offset, static_cast<int>(count));
            if (sent == SOCKET_ERROR)
            {
                return WSAGetLastError() == WSAEWOULDBLOCK; // 다음 writable 이벤트에서 재시도.
            }
            if (sent <= 0 || static_cast<size_t>(sent) > count)
            {
                return false;
            }
            pending.offset += sent;
            budget -= sent;
            if (pending.offset == pending.bytes->size())
            {
                const size_t allocatedBytes = pending.bytes->size();
                m_pending.pop_front();
                complete(allocatedBytes);
            }
        }
        // 한 연결에서 송신이 많아서 독점될 수도 있으므로
        return true;
    }

private:
    struct PendingSend
    {
        SharedPacketBytes bytes;
        size_t offset;
    };

    std::deque<PendingSend> m_pending;
};

class Session
{
public:
    Session()
        : m_assembler(&Session::CreateServerBody,
                      [](const PacketHeader& h)
                      {
                          return ValidateHeader(h, true);
                      })
    {
    }

    AssembleResult OnRecv(const char* buffer, int len, std::vector<ServerToManagerPacket>& packets);
    SendBufferQueue sendQueue;

private:
    static std::shared_ptr<IBody> CreateServerBody(EPacketType type, const char* bodyData, UINT16 bodyLength);
    PacketAssembler<ServerToManagerPacket> m_assembler;
};
