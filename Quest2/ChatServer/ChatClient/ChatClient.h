// ChatClient.h
#pragma once
#include <WinSock2.h>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>

#include "ClientInternalTypes.h"
#include "PacketAssembler.h"

#pragma comment(lib, "ws2_32.lib")

// TODO : namespace 고민
class ChatClient
{
public:
    ChatClient();
    ~ChatClient();

    bool ConnectServer(const std::string& serverIP, unsigned short port);
    void Run();

private:
    using ResponseHandler = std::function<void(std::shared_ptr<IBody>)>;

    void RegisterResponseHandlers();

    enum class EState : uint8_t
    {
        NewLoggingIn,
        OldLoggingIn,
        WaitingLogIn,

        InLobby,
        InRoom
    };

private:
    void ReceiveRespond();
    void ProcessPacket(EPacketType messageType, std::shared_ptr<IBody> receivePacket);

    void ResLogin(std::shared_ptr<IBody> packet);
    void ResCreateRoom(std::shared_ptr<IBody> packet);
    void ResJoinRoom(std::shared_ptr<IBody> packet);
    void ResBroadcastChat(std::shared_ptr<IBody> packet); // ReqChat이랑 짝이 아님, 브로드캐스팅용
    void ResPendedChat(std::shared_ptr<IBody> packet);
    void ResUserList(std::shared_ptr<IBody> packet);
    void ResRoomList(std::shared_ptr<IBody> packet);
    void ResWhisperDeliveredRes(std::shared_ptr<IBody> packet);
    void ResWhisperFailedRes();

    void ReqLogin(const std::string& username, bool isOldOkay);
    void ReqCreateRoom(const std::string& roomTitle);
    void ReqJoinRoom(UINT roomID);
    void ReqQuitRoom();
    void ReqUserList();
    void ReqRoomList();
    void ReqWhisper(std::string_view targetUser,
                    std::string_view message); // string view : 문자열 소유X, readonly view <-> const std::string& :
                                               // 불필요한 메모리 복사 방지
    void ReqChat(const std::string& message);
    void ReqUpdateLastReadChat();

    static std::shared_ptr<IBody> CreateClientBody(EPacketType type, const char* bodyData, UINT16 bodyLength);

    void GetLine(std::string& outInput, EState& outState);

    bool IsValidUsername(std::string& username);
    bool IsValidStringLength(std::string& str, int maxLength, std::string log);

private:
    SOCKET m_serverSocket = INVALID_SOCKET; // 서버와의 통신 소켓
    std::string m_username = "";            // 로그인 이후 유저 ID
    UINT m_joinedRoomID = LOBBY_ID;
    UINT m_lastReadChatIndex = 0; // TODO : atomic하게 증가?

    std::mutex m_mutex;
    std::condition_variable
        m_condition; // wait() -> mutex 언락 -> sleep(notify 올 때까지) -> 깸 -> mutex 락 -> predicate 확인
    std::atomic<EState> m_state{EState::NewLoggingIn}; // TODO : atomic 점검
    std::thread m_receiveThread;

    PacketAssembler<ClientPacket> m_packetAssembler;
    std::unordered_map<EPacketType, ResponseHandler> m_responseHandlers;

    // TODO false sharing 실제 effect 성능 좀
    alignas(64) std::atomic<bool> m_isRunning = false; // TODO : server쪽에도 있었던 것 같은데.
};
