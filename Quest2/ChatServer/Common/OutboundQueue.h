#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>
#include "Protocol.h"

using ConnectionId = uint64_t;
constexpr ConnectionId INVALID_CONNECTION = 0;

struct SendLimits
{
    // 작은 채팅 패킷 기준 연결당 256 KiB, 전체 4 MiB까지만 서버 메모리에 대기시킨다.
    size_t perConnectionBytes = 256 * 1024;
    size_t totalBytes = 4 * 1024 * 1024;
};

class OutboundQueue
{
public:
    explicit OutboundQueue(SendLimits limits = {}) : m_limits(limits)
    {
    }

    void Register(ConnectionId id)
    {
        std::lock_guard lock(m_mutex);
        m_connections.try_emplace(id);
    }

    bool TrySend(ConnectionId id, SharedPacketBytes bytes)
    {
        std::lock_guard lock(m_mutex);
        auto it = m_connections.find(id);
        if (it == m_connections.end() || it->second.closeRequested || !bytes || bytes->empty())
        {
            return false;
        }
        auto& entry = it->second;
        // Network에 넘기기 전 inbox부터 송신 완료까지 같은 예산에 포함한다.
        // Session 큐만 제한하면 producer inbox가 먼저 무한 증가함.
        // 대기/블로킹 대신 초과 요청의 연결을 종료 대상으로 표시한다. 전체 한도에서도 현재 요청 연결을 종료한다.
        if (bytes->size() > m_limits.perConnectionBytes - entry.bytes ||
            bytes->size() > m_limits.totalBytes - m_totalBytes)
        {
            entry.closeRequested = true;
            ++m_overflowCount;
            return false;
        }
        entry.bytes += bytes->size();
        m_totalBytes += bytes->size();
        entry.inbox.push_back(std::move(bytes));
        return true;
    }

    template <typename Body>
    bool Send(ConnectionId id, EPacketType type, const Body& body)
    {
        return TrySend(id, SerializePacket(type, body));
    }

    void RequestClose(ConnectionId id)
    {
        std::lock_guard lock(m_mutex);
        if (auto it = m_connections.find(id); it != m_connections.end())
        {
            it->second.closeRequested = true;
        }
    }

    bool Take(ConnectionId id, std::deque<SharedPacketBytes>& out)
    {
        std::lock_guard lock(m_mutex);
        auto it = m_connections.find(id);
        if (it == m_connections.end() || it->second.closeRequested)
        {
            return false;
        }
        out.swap(it->second.inbox);
        return true;
    }

    void Complete(ConnectionId id, size_t bytes)
    {
        std::lock_guard lock(m_mutex);
        if (auto it = m_connections.find(id); it != m_connections.end())
        {
            it->second.bytes -= bytes;
            m_totalBytes -= bytes;
        }
    }

    void Unregister(ConnectionId id)
    {
        std::lock_guard lock(m_mutex);
        if (auto it = m_connections.find(id); it != m_connections.end())
        {
            m_totalBytes -= it->second.bytes;
            m_connections.erase(it);
        }
    }

    size_t PendingBytes() const
    {
        std::lock_guard lock(m_mutex);
        return m_totalBytes;
    }

    size_t OverflowCount() const
    {
        std::lock_guard lock(m_mutex);
        return m_overflowCount;
    }

private:
    struct Entry
    {
        std::deque<SharedPacketBytes> inbox;
        size_t bytes = 0;
        bool closeRequested = false;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<ConnectionId, Entry> m_connections;
    SendLimits m_limits;
    size_t m_totalBytes = 0;
    size_t m_overflowCount = 0;
};
