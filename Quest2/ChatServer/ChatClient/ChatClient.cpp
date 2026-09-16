// ChatClient.cpp
#include "ChatClient.h"
#include <limits>   // std::numeric_limits<UINT>::max
#include <charconv> // from_chars
#include <functional>
#include <unordered_map>
#include <utility>

namespace
{
using BodyCreator = std::function<std::shared_ptr<IBody>(const char*, UINT16)>;

template <typename BodyType>
std::shared_ptr<IBody> CreateTypedBody(const char* bodyData, UINT16 bodyLength)
{
    return CreateBody<BodyType>(bodyData, bodyLength);
}

const std::unordered_map<EPacketType, BodyCreator>& GetClientBodyCreators()
{
    static const std::unordered_map<EPacketType, BodyCreator> bodyCreators = {
        {EPacketType::LoginRes, CreateTypedBody<LoginResBody>},
        {EPacketType::RoomCreateRes, CreateTypedBody<CreateRoomResBody>},
        {EPacketType::RoomJoinRes, CreateTypedBody<JoinRoomResBody>},
        {EPacketType::ChattingBroadcast, CreateTypedBody<ChattingBroadcastBody>},
        {EPacketType::ChattingPended, CreateTypedBody<ChattingBroadcastBody>},
        {EPacketType::WhisperFailRes, CreateTypedBody<WhisperFailedResBody>},
        {EPacketType::WhisperDeliveredRes, CreateTypedBody<WhisperDeliveredResBody>},
        {EPacketType::RoomlistRes, CreateTypedBody<RoomListResBody>},
        {EPacketType::UserlistRes, CreateTypedBody<UserListResBody>},
    };

    return bodyCreators;
}
} // namespace

ChatClient::ChatClient()
    : m_serverSocket(INVALID_SOCKET), m_packetAssembler(&ChatClient::CreateClientBody,
                                                        [](const PacketHeader& h)
                                                        {
                                                            return ValidateHeader(h, false);
                                                        })
{
    RegisterResponseHandlers();
}

ChatClient::~ChatClient()
{
    m_isRunning.store(false, std::memory_order_release); // TODO : Memory Barrier: Acquire-Release semantics

    closesocket(m_serverSocket);

    if (m_receiveThread.joinable())
    {
        m_receiveThread.join();
    }

    WSACleanup();

    std::cout << "Client 소통 끝\n";
}

bool ChatClient::ConnectServer(const std::string& serverIP, unsigned short port)
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    m_serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr);

    if (connect(m_serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cout << "서버 Connect가 안 된다..." << std::endl;
        closesocket(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;

        WSACleanup(); // WSAStartup이 있으니 여기에 Cleanup
        return false;
    }

    m_isRunning.store(true, std::memory_order_release); // Server와 통신 가능 상태
    return true;
}

void ChatClient::Run()
{
    std::cout << "서버에 연결되었습니다." << std::endl;

    // 수신 시작
    m_receiveThread = std::thread(&ChatClient::ReceiveRespond, this);

    // 채팅 입력
    std::string userInput;
    while (m_isRunning.load(std::memory_order_acquire))
    {
        EState curState = m_state.load(std::memory_order_acquire);

        if (curState == EState::NewLoggingIn)
        {
            while (true)
            {
                std::cout << "사용할 닉네임을 입력하세요: ";
                GetLine(userInput, curState);
                if (IsValidUsername(userInput))
                {
                    break;
                }
            }

            m_username = userInput;
            ReqLogin(userInput, false);
            continue;
        }
        else if (curState == EState::OldLoggingIn)
        {
            std::cout << "이미 있는 회원 이름입니다. 접속하시겠습니까? (y/n): ";
            GetLine(userInput, curState);
            if (userInput == "y" || userInput == "Y")
            {
                ReqLogin(m_username, true);
            }
            else
            {
                m_state = EState::NewLoggingIn;
            }
            continue;
        }
        else if (curState == EState::WaitingLogIn)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait(lock,
                             [this]()
                             {
                                 return m_state.load(std::memory_order_acquire) != EState::WaitingLogIn;
                             });
            continue;
        }

        // 로그인 된 거 보장
        GetLine(userInput, curState);

        if (userInput == "/quit" && curState == EState::InLobby)
        {
            break;
        }

        if (userInput.rfind("/", 0) == 0) // 명령어
        {
            if (userInput.rfind("/create ", 0) == 0) // TODO : 함수에 넣어버릴까?
            {
                if (curState != EState::InLobby)
                {
                    std::cout << "[오류]  현재 " << m_joinedRoomID
                              << "번 방에 있습니다. 나와서 만들어주세요.(예: / quit)" << std::endl;
                    continue;
                }

                std::string roomTitle = userInput.substr(8); // /create  이후의 문자열 추출
                if (roomTitle.empty())
                {
                    std::cout << "[오류] 방 제목을 입력해주세요. (예: /create MyRoom)" << std::endl;
                    continue;
                }

                if (!IsValidStringLength(roomTitle, MAX_ROOM_TITLE_LEN, "방 제목"))
                {
                    continue;
                }

                else
                {
                    ReqCreateRoom(roomTitle);
                }
            }
            else if (userInput.rfind("/join ", 0) == 0)
            {
                if (curState != EState::InLobby)
                {
                    std::cout << "[오류]  현재 " << m_joinedRoomID
                              << "번 방에 있습니다. 나와서 들어가주세요.(예: / quit)" << std::endl;
                    continue;
                }
                try
                {
                    std::string roomStr = userInput.substr(6);
                    UINT roomID = 0;
                    auto [ptr, error] =
                        std::from_chars(roomStr.data(), roomStr.data() + roomStr.size(), roomID); // 문자열 변환

                    if (error == std::errc::invalid_argument ||
                        error == std::errc::result_out_of_range) // 숫자로 변환 안ㄷ ㅚㄹ 때, 타입의 범위를 초과할 때
                    {
                        std::cout << "[오류] 유효하지 않은 방 번호입니다." << std::endl;
                        continue;
                    }
                    if (roomID > (std::numeric_limits<UINT>::max)())
                    {
                        throw std::out_of_range("Room ID가 너무 큽니다.");
                    }

                    ReqJoinRoom(roomID);
                }
                catch (const std::invalid_argument& e)
                {
                    std::cout << "[오류] 유효한 방 번호를 입력해주세요. (숫자만 가능)" << std::endl;
                }
                catch (const std::out_of_range& e)
                {
                    std::cout << "[오류] 방 번호가 너무 큽니다." << std::endl;
                }
            }
            else if (userInput == "/quit")
            {
                ReqQuitRoom();
            }
            else if (userInput == "/roomlist")
            {
                ReqRoomList();
            }
            else if (userInput == "/userlist")
            {
                ReqUserList();
            }
            else if (userInput.rfind("/whisper ", 0) == 0)
            {
                std::string_view view(userInput);

                size_t targetStart = 9; // /whisper
                size_t messageStart = view.find(' ', targetStart);

                if (messageStart != std::string_view::npos)
                {
                    std::string_view targetUser = view.substr(targetStart, messageStart - targetStart);
                    std::string_view message = view.substr(messageStart + 1);

                    if (!targetUser.empty() && !message.empty())
                    {
                        ReqWhisper(targetUser, message);
                    }
                    else
                    {
                        std::cout << "[오류] 대상 이름이나 메시지가 비어있습니다." << std::endl;
                    }
                }
                else
                {
                    std::cout << "[오류] 사용법: /whisper [대상] [메시지] (예: /whisper User1 안녕)" << std::endl;
                }
            }
            else
            {
                std::cout << "[오류] 알 수 없는 명령어입니다." << std::endl;
            }
        }
        else
        {
            if (!IsValidStringLength(userInput, MAX_MESSAGE_LEN, "메시지"))
            {
                continue;
            }

            if (curState == EState::InRoom)
            {
                ReqChat(userInput);
            }
            else
            {
                std::cout << "먼저 방에 참여해야 채팅을 보낼 수 있습니다. (예: /join [방ID])" << std::endl;
            }
        }
    }
}

void ChatClient::ReceiveRespond()
{
    char tempBuffer[MAX_BUFFER_SIZE];
    while (m_isRunning.load(std::memory_order_acquire))
    {
        int receiveLength = recv(m_serverSocket, (char*)&tempBuffer, sizeof(tempBuffer), 0);

        if (receiveLength <= 0)
        {
            std::cout << "\n서버와 연결이 끊겼습니다." << std::endl;
            break;
        }

        std::vector<ClientPacket> contexts;
        if (m_packetAssembler.ProcessBytes(tempBuffer, receiveLength, contexts) != AssembleResult::Ok)
        {
            shutdown(m_serverSocket, SD_BOTH);
            break;
        }

        for (const auto& context : contexts)
        {
            ProcessPacket(context.type, context.body);
        }
    }
}

std::shared_ptr<IBody> ChatClient::CreateClientBody(EPacketType type, const char* bodyData, UINT16 bodyLength)
{

    const auto& bodyCreators = GetClientBodyCreators();
    auto it = bodyCreators.find(type);
    if (it == bodyCreators.end())
    {
        std::cout << static_cast<int>(type) << "메시지 타입 이거 뭐임? 해석 안 됨\n";
        return nullptr;
    }

    return it->second(bodyData, bodyLength);
}

void ChatClient::GetLine(std::string& outInput, EState& outState)
{
    outInput.clear();

    if (!std::getline(std::cin, outInput))
    {
        return;
    }

    outState = m_state.load(std::memory_order_acquire);

    if (outInput.length() >= MAX_MESSAGE_LEN)
    {
        std::cout << "[오류] 입력이 너무 길어 잘렸습니다." << std::endl;
        outInput.resize(MAX_MESSAGE_LEN - 1);
    }
}

bool ChatClient::IsValidUsername(std::string& username)
{
    size_t spacePos = username.find(' ');
    if (spacePos != std::string::npos)
    {
        std::cout << "닉네임에 공배 사용 불가. 공백 앞부분만 사용됩니다." << std::endl;
        username.erase(spacePos); // 공백 이후 삭제
    }

    if (username.empty())
    {
        std::cout << "[오류] 닉네임이 비었습니다. 다시 입력해주세요." << std::endl;
        return false;
    }

    if (username.length() >= MAX_USERNAME_LEN)
    {
        std::cout << "[오류] 너무 깁니다. " << MAX_USERNAME_LEN - 1 << "자 미만으로 입력!" << std::endl;
        return false;
    }

    return true;
}

bool ChatClient::IsValidStringLength(std::string& str, int maxLength, std::string log)
{
    if (str.length() >= maxLength)
    {
        std::cout << "[오류] " << log << " 너무 깁니다. " << maxLength - 1 << "자 미만으로 입력!" << std::endl;
        return false;
    }
    return true;
}

void ChatClient::RegisterResponseHandlers()
{
    m_responseHandlers[EPacketType::LoginRes] = [this](std::shared_ptr<IBody> packet)
    {
        ResLogin(std::move(packet));
    };

    m_responseHandlers[EPacketType::RoomCreateRes] = [this](std::shared_ptr<IBody> packet)
    {
        ResCreateRoom(std::move(packet));
    };

    m_responseHandlers[EPacketType::RoomJoinRes] = [this](std::shared_ptr<IBody> packet)
    {
        ResJoinRoom(std::move(packet));
    };

    m_responseHandlers[EPacketType::ChattingBroadcast] = [this](std::shared_ptr<IBody> packet)
    {
        ResBroadcastChat(std::move(packet));
    };

    m_responseHandlers[EPacketType::ChattingPended] = [this](std::shared_ptr<IBody> packet)
    {
        ResPendedChat(std::move(packet));
    };

    m_responseHandlers[EPacketType::WhisperDeliveredRes] = [this](std::shared_ptr<IBody> packet)
    {
        ResWhisperDeliveredRes(std::move(packet));
    };

    m_responseHandlers[EPacketType::WhisperFailRes] = [this](std::shared_ptr<IBody>)
    {
        ResWhisperFailedRes();
    };

    m_responseHandlers[EPacketType::RoomlistRes] = [this](std::shared_ptr<IBody> packet)
    {
        ResRoomList(std::move(packet));
    };

    m_responseHandlers[EPacketType::UserlistRes] = [this](std::shared_ptr<IBody> packet)
    {
        ResUserList(std::move(packet));
    };
}

void ChatClient::ProcessPacket(EPacketType messageType, std::shared_ptr<IBody> packet)
{
    auto it = m_responseHandlers.find(messageType);
    if (it == m_responseHandlers.end())
    {
        std::cout << (int)messageType << "메시지 타입 이거 뭐임?\n";
        return;
    }

    it->second(std::move(packet));
}

void ChatClient::ResLogin(std::shared_ptr<IBody> packet)
{
    auto resBody = std::static_pointer_cast<LoginResBody>(packet);

    if (resBody->isCanLogIn) // TODO: 프로토콜상 거부 상태 세분화 필요
    {
        m_joinedRoomID = resBody->roomID;

        if (resBody->isNew)
        {
            std::cout << "\n새 회원 접속 성공! username: " << m_username << "\n";
        }
        else
        {
            std::cout << "\n기존 회원 접속 성공! username: " << m_username << "\n";
        }

        m_state = (m_joinedRoomID == LOBBY_ID) ? EState::InLobby : EState::InRoom;
    }
    else
    {
        m_state = EState::OldLoggingIn;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_condition.notify_one();
}

void ChatClient::ResCreateRoom(std::shared_ptr<IBody> packet) // 아래 JoinRoom이랑 로직 중복
{
    auto resBody = std::static_pointer_cast<CreateRoomResBody>(packet);

    if (resBody->isNew)
    {
        m_joinedRoomID = resBody->newRoomID;
        m_state = EState::InRoom; // TODO : state 언제 언제 바뀌는지 꼬이지 않게!

        std::cout << "방 만들기 성공! RoomID: " << m_joinedRoomID << "에 참여 중" << std::endl;
    }
    else
    {
        std::cout << "방 만들기 실패!" << std::endl;

        m_joinedRoomID = LOBBY_ID;
        m_lastReadChatIndex = 0;

        m_state = EState::InLobby;
    }
}

void ChatClient::ResJoinRoom(std::shared_ptr<IBody> packet)
{
    auto resBody = std::static_pointer_cast<JoinRoomResBody>(packet);

    if (resBody->isSuccess)
    {
        m_state = EState::InRoom;

        std::cout << "방에 성공적으로 참여했습니다." << std::endl;
    }
    else
    {
        m_joinedRoomID = LOBBY_ID;
        m_lastReadChatIndex = 0;

        m_state = EState::InLobby;

        std::cout << "방 참여에 실패했습니다. (없거나 인원이 다 찼음)" << std::endl; // TODO : m_rooms에 인원 기록?
    }
}

void ChatClient::ResBroadcastChat(std::shared_ptr<IBody> packet)
{
    auto resBody = std::static_pointer_cast<ChattingBroadcastBody>(packet);

    m_lastReadChatIndex++;

    if (resBody->sendingUser == m_username) // 내가 보낸 메시지는 다시 받지 않음, 오류 확인용
    {
        // std::cout << "(by me)";
        return;
    }
    std::cout << "[User " << resBody->sendingUser << "]: " << resBody->message << std::endl;
}

void ChatClient::ResPendedChat(std::shared_ptr<IBody> packet)
{
    auto resBody = std::static_pointer_cast<ChattingBroadcastBody>(packet);

    std::cout << "[User " << resBody->sendingUser << "]: " << resBody->message << std::endl;
}

void ChatClient::ResUserList(std::shared_ptr<IBody> packet)
{
    auto resBody = std::static_pointer_cast<UserListResBody>(packet);

    std::cout << "--- Live User List (" << resBody->userCount << ") ---" << std::endl;
    for (UINT16 i = 0; i < resBody->userCount; i++)
    {
        std::cout << i << ": " << resBody->users[i] << std::endl;
    }
}

void ChatClient::ResRoomList(std::shared_ptr<IBody> packet)
{
    auto resBody = std::static_pointer_cast<RoomListResBody>(packet);

    std::cout << "--- Room List (" << resBody->roomCount << ") ---" << std::endl;

    for (UINT16 i = 0; i < resBody->roomCount; i++)
    {
        std::cout << i << ": " << resBody->rooms[i] << std::endl;
    }
}

void ChatClient::ResWhisperDeliveredRes(std::shared_ptr<IBody> packet)
{
    auto resBody = std::static_pointer_cast<WhisperDeliveredResBody>(packet);

    std::cout << "[귓속말 from " << resBody->sendingUser << "]: " << resBody->message << std::endl;
}

void ChatClient::ResWhisperFailedRes()
{
    std::cout << "귓속말 전송 실패: 대상을 찾을 수 없거나 오프라인입니다." << std::endl;
}

void ChatClient::ReqLogin(const std::string& username, bool isOldOkay)
{
    LoginReqBody reqBody;
    reqBody.isOldOkay = isOldOkay;
    strncpy_s(reqBody.username, MAX_USERNAME_LEN, username.c_str(), _TRUNCATE);

    m_state = EState::WaitingLogIn;
    SendPacket(m_serverSocket, EPacketType::LoginReq, reqBody);
}

void ChatClient::ReqCreateRoom(const std::string& roomTitle)
{
    CreateRoomReqBody reqBody;
    strncpy_s(reqBody.roomname, MAX_ROOM_TITLE_LEN, roomTitle.c_str(), _TRUNCATE);

    SendPacket(m_serverSocket, EPacketType::RoomCreateReq, reqBody);
}

void ChatClient::ReqJoinRoom(UINT roomID)
{
    JoinRoomReqBody reqBody;
    reqBody.roomID = roomID;
    m_joinedRoomID = roomID; // 참여 요청한 방 ID를 기억

    SendPacket(m_serverSocket, EPacketType::RoomJoinReq, reqBody);
}

void ChatClient::ReqQuitRoom()
{
    QuitRoomReqBody reqBody;
    reqBody.roomID = m_joinedRoomID;

    SendPacket(m_serverSocket, EPacketType::RoomQuitReq, reqBody);
    std::cout << "나갑니다..." << std::endl;

    m_joinedRoomID = LOBBY_ID; // TODO : 초기화를 여기서?
    m_lastReadChatIndex = 0;

    m_state = EState::InLobby;
}

void ChatClient::ReqUserList()
{
    UserListReqBody reqBody;

    SendPacket(m_serverSocket, EPacketType::UserlistReq, reqBody);
    std::cout << "유저 리스트 요청합니다..." << std::endl;
}

void ChatClient::ReqRoomList()
{
    RoomListReqBody reqBody;

    SendPacket(m_serverSocket, EPacketType::RoomlistReq, reqBody);
    std::cout << "룸 리스트 요청합니다..." << std::endl;
}

void ChatClient::ReqWhisper(std::string_view targetUser, std::string_view message)
{
    WhisperReqBody reqBody;

    if (targetUser.length() > static_cast<size_t>(MAX_USERNAME_LEN - 1)) // TODO : 검증을 좀 더 일찍해도 될 듯?
    {
        std::cout << "User 이름이 너무 깁니다! " << MAX_USERNAME_LEN - 1 << " 글자 제한\n";
        return;
    }

    if (message.length() > static_cast<size_t>(MAX_MESSAGE_LEN - 1)) // TODO : 검증을 좀 더 일찍해도 될 듯?
    {
        std::cout << "메시지가 너무 깁니다! " << MAX_MESSAGE_LEN - 1 << " 글자 제한\n";
        return;
    }

    std::memcpy(reqBody.receivingUser, targetUser.data(),
                targetUser.length());                  // string_view는 .data()로 내부 포인터 접근 가능 (길이 제한 복사)
    reqBody.receivingUser[targetUser.length()] = '\0'; // 뒤 \0 문자 보장

    std::memcpy(reqBody.message, message.data(), message.length());
    reqBody.message[message.length()] = '\0';

    SendPacket(m_serverSocket, EPacketType::WhisperReq, reqBody);
}

void ChatClient::ReqChat(const std::string& message)
{
    ChattingReqBody reqBody;
    reqBody.roomID = m_joinedRoomID;
    strncpy_s(reqBody.message, MAX_MESSAGE_LEN, message.c_str(), _TRUNCATE);

    SendPacket(m_serverSocket, EPacketType::ChattingReq, reqBody);
}

void ChatClient::ReqUpdateLastReadChat() // TODO : 곧 이 기능으로 마이그레이션
{
    ChattingConfirmReqBody reqBody;
    reqBody.roomID = m_joinedRoomID;
    reqBody.lastReadIndex = m_lastReadChatIndex;

    SendPacket(m_serverSocket, EPacketType::ChattingConfirm, reqBody);
}
