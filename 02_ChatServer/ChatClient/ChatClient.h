// ChatClient.h
#pragma once
#include "Protocol.h"
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

// main 스레드: 입력/송신, receive 스레드: 수신/응답 처리
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
                    std::string_view message);
    void ReqChat(const std::string& message);

    static std::shared_ptr<IBody> CreateClientBody(EPacketType type, const char* bodyData, UINT16 bodyLength);

    void GetLine(std::string& outInput, EState& outState);

    bool IsValidUsername(std::string& username);
    bool IsValidStringLength(std::string& str, int maxLength, std::string log);

private:
    SOCKET m_serverSocket = INVALID_SOCKET; // 서버와의 통신 소켓
    std::string m_username = "";            // 로그인 이후 유저 ID
    UINT m_joinedRoomID = LOBBY_ID;
    UINT m_lastReadChatIndex = 0;

    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::atomic<EState> m_state{EState::NewLoggingIn}; // main 스레드가 읽고 receive 스레드가 바꾼다
    std::thread m_receiveThread;

    PacketAssembler<ClientPacket> m_packetAssembler;
    std::unordered_map<EPacketType, ResponseHandler> m_responseHandlers;

    // false sharing 방지
    alignas(64) std::atomic<bool> m_isRunning = false;
};
