[ 전체 구조 ]
====Chat Client Side
Chat Client Class : main Thread, receive Thread로 나뉘어져 있음.

====Chat Server Side
Chat Server Class : 네트워크 연결, 패킷 파싱(Session.h과 PacketAssembler.h) 담당.Shared ptr로 ChatManager 소유
Chat Manager Class : Queue에 들어온 Packet을 패킷 타입 별로 처리, 특정 채팅방에 관련된 건 ChatRoom에 넘김, ChatRoom을 shared ptr map으로 소유, client로 send() 가능
Chat Room Class : Queue에 들어온 Packet을 패킷 타입 별로 처리, Broadcast 해야할 채팅 메시지는 벡터를 돌면서 직접 send() 한다.

====Server Thread 1 - 1 - N 구조

1. Server Thread : Server 객체 만드는 Thread 하나: Client Msg를 Queue에 넣어줌 (ChatServer.cpp)
2. ChatManager Thread : Chat관련 명령어 관리하는 Thread 하나 : /login ...(ServerLogic.h)
3. Room Thread : 채팅방에서 메시지 Broadcast하는 Thread N개 : Room 당 하나의 Thread

====Packet 종류
Client는 Protocol.h의 ClientPacketContext를 활용하고,
Server쪽은 InternalTypes.h의 ServerToManagerPacket과 ManagerToRoomPacket을 씁니다.
각각은 ChatServer가 ChatManager에게 Packet을 줄 때, 그리고 ChatManager가 ChatRoom에게 Packet을 줄 때 쓰는 구조체입니다.

Log(오름차순) --------------------------------------------------------------------------------------------------------------------------------------------------------------

2026.03.25 17:40 강서우 멘토님 코드 리뷰 이후 내용은 앞에 기호 -  를 사용

[ 리뷰 내용 ]
- ChatClient의 connected 함수에 WSACLOSE 함수 누락
	: ChatClient 자체가 하나의 행위를 하는 객체다. 현재는 main()에서 return 해버리기 때문에 문제가 되지 않지만, 만약 다양한 다른 IP 등에서 ChatClient를 부른다면 레퍼런스 카운트 및 리소스 해제 제대로 안 된 상태가 될 수 있다.
	TODO - WSACLOSE 함수 원리 공부

- Thread 1:1:N이 각각 용도에 맞게 쓰이지 않았다
	TODO -  Command Queue 개념을 각 Client, Server에 넣어라
		: 패킷을 수행하는 거, 패킷 이벤트 발생시키는 개념 분리, State는 Instance가 된다
		: ChatClient는 시스템 코어다. 시스템 코어란, 구성이 잡히면 콘텐츠가 변경되더라도 수정될 일이 없어야 한다. 역할에 대한 분리가 필요하다

- Protocol.h의 constexpr는 file io로 Data Driven하게 사용하자
	: 코드에서 데이터를 바꾸면 안된다. 코드는 건드리지 않고, 데이터만 바꿀 수 있게 하는 설계가 장기적으로는 필수적이다.
	TODO - 파일 IO 개념 적용

- 구조체와 ENUM이 1:1 매핑이라면, 둘 중 하나만 존재햐애한다
	TODO - ENUM 삭제 필요, 중복되는 자료를 최소화하는 게 실무에서는 중요한 듯

- 이 상황이 발생하지 않는다라고 가정하는 건 나쁜 태도다
	: IBody에서 가상 소멸자를 없앤 이유가 자식 구조체에 힙할당 타입이 없어서라면, 나쁜 설계다. 실제는 실제 구조체의 타입의 소멸자가 항상 불리기 때문에 필요없는 것. C++의 new를 하면 호출이 안 되는 것. 
	TODO - virtual 소멸자 등의 동작 공부, 어떻게 소멸하는지 공부

- 에러 핸들링 부족
	: 각 에러 및 실패가 떴다면, 실패 이유를 꼭 넣어주기. 과해도 나쁘지 않다. 배열 같은 것도 매핑해서 처리 필요
	TODO - WhisperFail 등의 body에 실패 이유 넣기

- TODO 분리 필요
	TODO - 앞으로 동작 알아야할 것은 LEARN, 스펙 및 UX를 고려한 기능 확장 고민은 WORK, 설계 및 성능을 고려한 기능 재구성 필요 및 고민은 TODO

- Array와 Vector의 차이?
	: Array도 힙이다! Array는 Direct Access가 아니라 더 느리다. 다만 용량 확장 시에는 뒤에 붙이기만 하면 돼서 resize를 하는 Vector보다 빠르다. 그러나 이런 하드웨어 레벨은 보통 큰 병목이 되지 못한다. 문제는 언제나 컨텐츠 로직과 에셋 등이 느린 거다...
	TODO - Array와 Vector의 차이 공부

- Command Pattern에 대한 팁
	: ChatClient에서 Enum 기준으로 State 바꾸는 거, Enum을 최소화하는 게 중요
	: 구조체 자체가 하나의 타입이니 그것만으로 그 패킷이 왔을 때 어떤 행동을 하겠다로 바로 이어져야 한다. 패킷 핸들러 같은 걸로. 또한 현재는 enum, body, createbody, parsing ... 추가가 복잡한데, 추가가 굉장히 단순하고 용이하게 제작하라. Command Queue를 쓰면 중간 과정이 단순해 진다.
	: 제작한다면, 로비씬, 룸씬 등의 객체가 있을 것이다. 거기 안에서 각자 하는 걸로. State가 맞는 녀석으로 변환만 해주면 되고, 피처 확장 시 객체만 추가하면 되는 일.
	TODO - Command Queue 패턴 적용

- Mutex는 병목의 주범
	: Lock Free하게 만드려고 노력했는데 이게 맞는 방향인지? 질문에 대해, Mutex는 최소화하는 게 좋다.
	현재 Queueing과 Receiving도 Lock 안 걸 수 있다, 로직적으로. send(), recv()에서 mutex 거는데, 사실 mutex 거는 이유는 여러 스레드에서 동시 접근하기 때문이다.
	TODO - Queueing, Receiving을 Lock Free하게 만들기


[ 현재 코드에 대해 고민하고 있는 부분 ]

1. packet의 body를 파싱할 때, shared ptr과 IBody를 썼는데, 또 memcpy 역시 쓰고 있어서(Protocol.h의 CreateBody함수와 PacketAssebler.h) 아예 raw ptr로 할지, shared ptr로 할지 고민 중입니다. shared ptr과 IBody로 구조체들 다형성을 챙기고, memcpy로 단순 복사하려고 했는데 오히려 복사 비용이 너무 큰 것 같습니다...
-> 
- 굳이? 물론 의도적으로 사보타지 패킷을 던졌을 때를 대비하기 위할 때는 Shared ptr이 맞다. 만약 thread safe하면 atomic 해야하기 때문에 속도 챙겨야 하는 상황에서는 부적절하다. 프로덕트 성향에 따라 다르지만, 일반 응용 어플리케이션에서는 굳이 필요없다. 굉장히 무거우면 고려해봐야겠지만.
	TODO - shared ptr 걷어내기 + shared ptr의 ref count가 0이 될 때 어떤 일이 일어나는지 공부, shared ptr이 thread safe한지 공부

2. 각 서버 클래스 사이 메시지 큐가 들어있는 함수만 Public으로 열어놔, 기타 자료구조에 대해 lock free하게 제작했는데 좋은 설계 방향인지 고민 됩니다. queue에 무조건 쌓여서 룸 삭제나 유저 비활성화 같은, 곧바로 처리해야할 것 같은 일이 지연됩니다. lock을 적절하게 열고 곧바로 호출할 수 있게 할까요?
- Mutex는 병목의 주범... 각 삭제 및 비활성화 경우에 대한 핸들링을 잘 하면 되는 일.

3. 유저가 밀린 톡을 받을 때, 현재 기준이 서버 기준으로 유저의 disconnected 패킷을 처리할 때의 메시지 인덱스로 해놨는데, 이러면 누락된 전송 톡이 있을 것 같습니다. 유저가 n시간마다 특정 인덱스까지 받았다는 확인 ack을 날리게 하는 걸로 고민 중입니다. 이때는 갱신하지 못하고 disconnected가 된 경우에 중복해서 톡을 또 받겠지만, 누락보다는 나을 것 같습니다. 다만 과제의 의도에 맞는지 고민 중입니다.
- 의도에서 벗어난 것 같다. 이건 어플 바이 어플이다. 기능에 대한 중요도 판단 필요. 또한, 프로파일링이 필요한 부분
	TODO - 정상 종료할 때 알려주는 로직까지는 제작 필요.

4. Whisper 기능을 어느 Thread에서 할지 고민해보라고 했습니다. 현재는 그냥 중간에 있는 ChatManager Thread에서 담당하고, 전송이 안 된 귓말은 소실되게 했는데 이 Thread가 담당하는 게 맞는 건지 고민 됩니다. 귓말이 많으면 나머지 작업이 지연되기도 하고, 귓말 소실되지 않게 자료구조도 넣으면 무거워질 것 같습니다.
- 현재 Thread 구조 상으로는 다 애매하다. 별도로 하나 만들든가 해야한다.
	TODO - Whisper 전용 Thread 설계 및 제작

5. ChatManager.h에 자료구조 중 std::unordered_map< SOCKET, std::string> m_socketToUsername 과 std::unordered_map<std::string, UserData> m_users; 이 있는데, 후자는 영구히 저장되는 유저 데이터고 전자는 재접속 시 바뀔 소켓에 대비한 소켓-유저키 데이터입니다. 그러나 UserData안에 소켓을 따로 이미 저장하고 있어서, 전자의 자료 구조는 단순 O(1) 조회용입니다. O(1) 조회가 데이터 동기화 비용+중복 비용보다 더 가치가 있는지, 아니라면 매번  m_users를 돌아 소켓을 찾아야하는지 고민이 됩니다
- 중복 자료구조는 고려할 필요 없이 하나로 만드는 게 낫다. 그러나 이 과제는 DB가 따로 없고, m_users가 DB가 어쩔 수 없다. 하지만 만약 DB가 있다면, <SOCKET, UserData>가 최적일 듯.

[ 조금 소소한 고민 ]

1. ChatClient.cpp의 Run() 함수가 너무 긴 것 같아 입력 별 함수를 만들지 고민 중입니다.
- 위 내용과 동일
2. ChatClient.cpp에 state별로 바꾸는 게 state 흐름을 한 눈에 확인하기 어려워 실수하기 좋은 구조인 것 같아 고민이 됩니다.
- 위 내용과 동일
3. ChatManager가 ChatServer의 멤버 변수인데, 싱글톤이 되어야 하는 거 아닌가? 내가 ChatManager를 Manager 대우해주고 있나?
- 매니저 명칭과 싱글톤은 무관하다. 해당 객체가 무엇을 하는지 행위와 역할이 중요한 거다. Actor인데도 Manager 명칭 갖고 있는 애들도 있고... 싱글톤 자체도 안 좋다.
- 싱글톤이 최악인 점
	: static instance가 프로세스 라이프타임을 따라가고, 여러 고셍서 get instance를 하기 때문에 문제가 된다. 초기화 시점이 문제가 된다. 호출을 여러곳에서 하면 초기화를 어디서 해야하는지 팀플에서는 하나하나 까볼 수가 없다. 그럼에도 프로세스 당 하나의 싱글톤이 돌아가는 등, 싱글톤을 써야하는 경우가 있다.
	: 싱글톤을 올바르게 쓰려면, 프로세스가 아니라 특정한 컨텍스트 라이프타임이 필요하다. Unreal의 경우 5개의 서브 시스템이 각 명확한 컨텍스트 내에서 유니크하게 돌아간다. PIE 시작과 함께 생기고 끝과 함께 소멸하는 subsystem 등, 어떤 동작에서 초기화와 파괴의 맥락이 분명해야 한다. 

- 태도 면에서.... 
	TODO : 모르면 모른다고 확실히 말하는 게 중요하다. 아아~ 하면서 표정은 모르겠어요 하고 끌면 상대는 추정해야하고 결국 시간만 끌게 되는 것. 고민하고, 잘 모르겠거나 방금의 행동이 틀렸다고 생각하면 확실히 정정하자.
	TODO : 모든 코드에는 이유가 필요하다. 이유를 가지고 코드를 짜자...
	TODO : push의 작업 단위는 자주 업데이트 하는 게 좋다.	


2026.09.16 코드 점검

[ 개선 필요 ]

1. 재접속 메시지 ACK 처리
	: 지금은 Client가 실제로 받은 마지막 메시지가 아니라, Server에서 Disconnect Event를 처리한 시점의 message index를 저장함
	: 실제 연결이 끊긴 시점과 Disconnect Event를 처리하는 사이에 만들어진 메시지는 Client가 못 받았는데도 읽은 걸로 처리될 수 있음
	TODO - ChattingConfirm Packet을 실제로 사용해서 Client가 마지막으로 받은 message index를 Server에 알려주도록 제작
	TODO - 중복해서 받는 건 허용하더라도 메시지가 빠지는 경우는 없게 기준 정리

2. Client쪽 개선 필요
	- Server 연결 종료 시 Client 처리 부족
		: Receive Thread의 recv()가 실패하면 해당 Thread만 끝나고 m_isRunning은 false로 바뀌지 않음
		: 로그인 응답을 기다릴 수 있음
		: 일반 입력 중이었다면 끊어진 socket을 계속 사용할 수도 있음
		TODO - ReceiveRespond가 끝날 때 실행 상태 변경, condition_variable notify, socket 종료 이유 전달
		TODO - stdin EOF도 정상 종료로 처리

	- Client 공유 상태 동기화
		: m_joinedRoomID, m_lastReadChatIndex, m_username을 Main Thread와 Receive Thread에서 같이 접근함
		TODO - State 변경을 한 Thread에서만 하게 만들거나 Event Queue를 두고 한쪽에서 순서대로 처리

	- Command Queue, State 한 곳에 모으기
		: Server처럼 Packet Handler + Queue 구조
		TODO - 입력 Event와 수신 Packet Event를 Command Queue 하나로 합치기
		TODO - Login, Lobby, Room 단위로 State 객체/핸들러로 분리

	- /quit 결과 확인 Packet 없음
		: Client는 Server 결과를 받고 Lobby State로 바꿔야 함(정합성 이슈)
		TODO - RoomQuitRes를 추가해서 성공 응답을 받은 뒤 Client State 변경

3. /roomlist에서 방 제목 안 보여줌
	: /create [subject] <-> RoomListRes에는 room ID만 들어있음
	TODO - RoomList 응답에 room ID와 제목을 같이 넣기

4. 귓속말 Thread?
	: 현재 Whisper는 ChatManager Thread에서 처리 중
	: 기능은 동작하지만 귓속말이 많아지면 Manager의 다른 명령도 같이 밀릴 수 있음
	TODO - Whisper 전용 Thread/Queue를 만들지, 지금 구조로도 충분한지...

5. 실패 이유 Packet에 만들기
	: LoginRes, RoomJoinRes는 bool만 있고 WhisperFailedResBody는 비어있음
	TODO - 응답 Body에 실패 이유 enum 또는 code 추가

6. Protocol 설정의 Data Driven화
	: IP, PORT, 이름/메시지 길이, 방 최대 인원 등이 constexpr
	TODO - 배포 환경에 따라 바뀔 수 있는 IP, PORT, 송신 한도 등은 설정 파일로 분리 + 버전 관리 가능하게

7. EPacketType과 Body 타입 중복
	: Handler Table은 적용했지만 Packet 하나 추가할 때 enum, Body, Creator, Handler를 여러 군데 추가해야 함
	TODO - std::variant, typed handler table, serializer 구조 중 어떤 식으로 줄일지 검토

8. shared_ptr<IBody> 제거 안 함
	: 현재는 recv buffer와 Queue에 들어간 Packet의 수명이 다르기 때문에 일단 유지 중
	: raw pointer로 바꾸면 비동기 Queue에서 원본 buffer가 먼저 없어질 수 있어서 그대로 쓸 수는 없음
	TODO - std::variant나 owning byte buffer로 바꿀 가치가 있는지 판단

9. Queue를 완전히 Lock Free하게 만들지는 않음
	: Manager Queue, Room Queue, OutboundQueue에서 mutex 사용 중
	: 대신 상태 자체는 담당 Thread 하나만 바꾸게 하고, mutex는 Queue에 넣고 빼는 부분에만 사용함

10. 사용자 조회용 자료구조 중복
	: m_connectionToUsername과 m_users를 같이 유지하고 있음
	: DB가 없어 어쩔 수 없음. ConnectionId로 사용자를 바로 찾기 위한 용도로 사용 중, double 자료구조 괜찮나?
	TODO - 나중에 DB나 Session 저장 구조를 넣는다면 어떤 쪽에서 UserData를 소유할지 다시 정리

11. Winsock 초기화/정리
	: Connect 실패 경로에 WSACleanup은 추가했지만 Destructor에서도 다시 호출될 수 있음
	: WSAStartup, socket, inet_pton 각각 실패했을 때 확인 필요
	TODO - WSA 초기화 성공 여부를 멤버로 들고 있고 Cleanup은 정확히 한 번만 호출하게 수정


[ 코드 작성 Convention ]

- C++ Standard는 C++20 사용

- 들여쓰기는 Tab 대신 Space 4칸 사용

- 줄바꿈은 CRLF, 파일 마지막에는 줄바꿈 문자 유지

- 중괄호는 기존 코드와 동일하게 Allman Style 사용
	: 함수, class, namespace, if, for, while의 여는 중괄호를 다음 줄에 작성

- 한 줄짜리 함수와 제어문은 사용하지 않음
	: if 조건과 실행문, 함수 선언과 본문을 각각 줄바꿈해서 작성
	: 여러 동작을 한 줄에 붙이지 않음
	: if, else, for, while 본문은 한 줄이어도 중괄호 사용

- 한 줄은 120자를 기준으로 줄바꿈
	: 긴 함수 인자, 조건식, lambda는 clang-format 결과를 따름

- Pointer와 Reference 기호는 Type에 붙임
	: UserData* user, const std::string& username 형태 사용

- 명명 규칙은 현재 구조 유지
	: Class, Struct, Enum, 함수는 PascalCase
	: 지역 변수와 함수 인자는 camelCase
	: 멤버 변수는 m_ 접두사 + camelCase
	: 상수는 UPPER_SNAKE_CASE

- include 순서는 강제로 재정렬하지 않음
	: 관련 Header를 먼저 두고, 표준/플랫폼 Header와 프로젝트 Header를 구분해서 작성

- .clang-format과 .editorconfig를 기준으로 전체 C++ 파일 형식을 통일
	: Visual Studio의 Format Document 또는 clang-format -i 명령 사용
