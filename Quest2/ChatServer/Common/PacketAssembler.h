#pragma once
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <vector>
#include "Protocol.h"

enum class AssembleResult
{
    Ok,
    InvalidPacket
};

template <typename ContextType>
class PacketAssembler
{
public:
    using BodyFactory = std::function<std::shared_ptr<IBody>(EPacketType, const char*, UINT16)>;
    using HeaderValidator = std::function<bool(const PacketHeader&)>;

    PacketAssembler(BodyFactory factory, HeaderValidator validator)
        : m_bodyFactory(std::move(factory)), m_headerValidator(std::move(validator))
    {
    }

    // 이전: 버퍼 압축 뒤 공간 재검사 없이 memcpy하여 오버런 가능.
    // 변경: 헤더/본문에 필요한 만큼만 복사하고 한 프레임씩 소비하기. 입력이 버퍼보다 커도 정상 패킷들의 합이면 처리
    // 이유: TCP 수신은 되게 하고, 잘못된 입력은 해당 연결만 종료
    AssembleResult ProcessBytes(const char* buffer, int len, std::vector<ContextType>& out)
    {
        const size_t originalCount = out.size();
        auto fail = [&]
        {
            m_invalid = true;
            m_size = 0;
            out.resize(originalCount); // 이번 입력에서 만든 패킷은 일괄 거부.
            return AssembleResult::InvalidPacket;
        };
        if (m_invalid || len < 0 || (len > 0 && buffer == nullptr))
        {
            return fail();
        }
        size_t remaining = static_cast<size_t>(len);
        while (remaining > 0)
        {
            const size_t target = m_size < sizeof(PacketHeader) ? sizeof(PacketHeader) : m_required;
            const size_t count = (std::min)(remaining, target - m_size);
            if (count > m_buffer.size() - m_size)
            {
                return fail();
            }
            std::memcpy(m_buffer.data() + m_size, buffer, count);
            m_size += count;
            buffer += count;
            remaining -= count;
            if (m_size < sizeof(PacketHeader))
            {
                continue;
            }
            PacketHeader header;
            std::memcpy(&header, m_buffer.data(), sizeof(header));
            if (!m_headerValidator(header))
            {
                return fail();
            }
            m_required = sizeof(PacketHeader) + header.bodyLength;
            if (m_required > m_buffer.size())
            {
                return fail();
            }
            if (m_size < m_required)
            {
                continue;
            }
            auto body = m_bodyFactory(header.type, m_buffer.data() + sizeof(PacketHeader), header.bodyLength);
            if (!body)
            {
                return fail();
            }
            auto& context = out.emplace_back();
            context.type = header.type;
            context.body = std::move(body);
            m_size = 0;
            m_required = sizeof(PacketHeader);
        }
        return AssembleResult::Ok;
    }

private:
    std::array<char, MAX_BUFFER_SIZE * 2> m_buffer{};
    size_t m_size = 0;
    size_t m_required = sizeof(PacketHeader);
    bool m_invalid = false;
    BodyFactory m_bodyFactory;
    HeaderValidator m_headerValidator;
};
