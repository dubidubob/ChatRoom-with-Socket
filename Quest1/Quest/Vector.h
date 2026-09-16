#pragma once
#include <new> // operator new
#include <limits> // std::numeric_limits
#include <utility> // std::move

#include <shared_mutex>
#include <mutex>

/*
* Thread Safe한 버전
* 
* 초기 생각:
* vector는 thread safe하게 만들지 않는다
* 
*	- 문제 1. zero overhead에 위반
*		두 스레드가 동시에 vector에 접근하여 수정하려고 하거나, add를 하면 동시에 쓰는 등의 data race에 의한 heap corruption이 일어난다
*		이를 방지하기 위해서는 각 쓰기 동작 당 mutex가 필요한데
*		읽기만 있는 경우에는 mutex가 필요 없고 불필요하게 multi thread의 병렬성을 깬다
*
*		쓰기는 mutex로 막고, 읽기는 열어두면 안 되는 이유는
*		동시에 읽으면 괜찮으나, 쓰기과 읽기가 동시에 일어나거나, growth 도중에 읽으면 그것도 heap corruption이 일어나기 때문.
*
*		모든 접근이 읽기만 할 때는 mutex 걸지 않게 하고, 쓰기를 한다면 읽기도 잠그는 방안
*		-> 그러나 single thread일 때 이러한 mutex 로직은 큰 오버헤드므로 아예 안 하고, multi thread logic에 책임 위임
*		
*		mutex가 오버헤드가 큰 이유: OS의 동작이기 때문. 
			1. mutex lock 걸 때마다 OS에서 context switching이 일어나기 때문
			2. mutex 안 잡은 thread는 sleep - wake up 과정에서 지연 발생
			3. 캐시 바운싱(스레드가 타 스레드가 업데이트한 사항을 받기 위해 L1,L2 등 메모리를 전부 최신 데이터로 읽어오는 과정)
	
	- 문제 2. T& return 시 reference invalidation
		참조나 포인터를 전달하는 순간 growth 주소 재할당 할 때 UB가 난다(이건 multi thread가 아니더라도 문제)
		어떤 순간에도 UB가 안 나게 보장받으려면 T& 삭제하고 T -> 사용성 급감 및 복사 성능 오버헤드

* 해결 과정:
	* 문제 1 은 과제 특성상 감수
	* 문제 2가 문제
		- 해결 1.  T& 반환을 살리되, 유저가 포인터로 vector를 선언해 실제 객체는 제자리에 있게 함(현재 방식)
			* 단점 :
			* 1.소유권 문제
			*	-> 스마트 포인터 -> shared ptr은 여전히 thread unsafe -> atomic shared ptr
			*   -> hazard pointer라는 것도 있는데 추후 논의: 소유권 없이 참조 안전하게 하자
			* 2. 캐시미스
			*	-> 메모리 청크로 캐시 지역성 높임(deque도 chunk, but 여긴 포인터를 거치지 않음)
			
		- 해결 2.  T& 대신 T로 한다 (초반에 시도했던 방식, commit c7398ba17e4e61c0b066a91a32b08e67e96c4075 참고)
			* 1. T& 리턴 삭제
			* 2. 수정은 modify safe 등 vector 내 mutex 수정 함수로 수행
			* 3. 복사 불가는 포인터로 수행 권유
			* 
			* 단점 : 복잡하고 불편함
			* T로 대체해 수정하기 위한 modify_safe, write_safe를 두어 vector 내부에서 mutex를 통해 수정하도록 했으나,
			*		- 람다 함수로 중복 mutex 통제 불가 및 디버깅 어려움으로 해결 1로 방향을 틀었다
			*		- main logic에서 일어나는 unordered map이 삽입되지 않는 문제로 일단 전부 Roll back
			*		- main 함수에서 복사 불가능한 thread를 unique ptr로 가리키게 만드는 걸로 대체
			
* 요약:
* RAM vs T& 반환
* 둘 중 하나는 포기해야하는 상황에서
* 그러나 RAM이 없으면 list와 다름이 없음. 
	=> 최대한 vector를 포인터로 만드는 방식은 지양했으나 사실상 mutex 오버헤드 + 구현 복잡도 때문에 포인터로 변경
* 
*/

template <typename T> // 여러 타입 T를 정의할 수 있게 해 중복 코드 작성 없이 코드 작성
class Vector { // 기존 stl vector와 차별을 위해 대문자 V 사용
public:
	using size_type = std::size_t;  /*
	* 1. 타입 별칭은 매개변수 등으로 외부 사용자에게도 노출되니 public 영역에 놓음
	* 2. 타입 별칭을 한 이유는 반복적인 사용 및 가독성을 위함/ 이식성을 위해서라기도 함, 그러나 std 해당 과제에서는 해당되지 않는 목적
	* 3. vector에서 size_type을 쓰는 이유는 unsigned int, int와 달리 운영체제에 따라 크기를 최적화 하는 부호 없는 정수이기 때문.
	*		size_type은 해당 시스템에서 객체가 가질 수 있는 최대 크기, 즉 항상 align이 맞취져있기 때문에 빠르게 동작
	*/

	Vector() = default; // 생성자에서 아무것도 안 한다.
	~Vector()
	{
		// if (m_begin) // => operator delete와 delete는 nullptr, 혹은 소멸자가 없어도 오류 안 낸다. 주석처리.
		{
			for(T* p = m_begin; p!=m_last; p++)
			{
				p->~T(); //m_begin 주소 기준 i*sizeof(T)만큼 전진해서 객체를 소멸시켜준다, 권한 반납
			}
			operator delete(m_begin); // 이후 할당된 메모리를 전부 회수한다.
			// 이때 객체 소멸과 메모리 회수는 다르다. 
			// 이 포인터에 할당된 메모리는 소멸자가 얼마나 호출되도 같다. 
			//  소멸자는 해당 메모리에 할당된 곳에 있는 객체에 대한 핸들러를 거두는 것이다.
		}
	}

	Vector(const Vector& other) = delete; // Vector v2(v1)을 할 시 둘다 m_begin 가리킴. 소멸자 호출 시 double free 버그가 터진다
	Vector& operator=(const Vector& other) = delete; // Vector v2 = v1을 할 시 둘다 m_begin 가리킴. 소멸자 호출 시 double free 버그가 터진다
	// 컴파일러가 기본 생성자, 파괴자, 복사 생성자, 복사 대입 연산자, 이동 생성자, 이동 대입 연산자를 자동 생성한다. 이후 복사 생성자, 복사 대입 연산자에 대해서 얕은 복사가 일어나 위 버그가 터진다. 깊은 복사를 구현해 override를 해야한다 추후 시간이 된다면...

	// Vector<int> v2 = std::move(v1);
	Vector(Vector&& other) noexcept // 해당 함수 내부에서는 절대 예외가 터질 일 없으니 컴파일러에게 예외 검토하지 말라고 하는 것. 단순 주소값 대입 및 nullptr 대입이기 때문, move if noexcept에 의해 반환값이 T&&이 된다
		: m_begin(other.m_begin), m_last(other.m_last), m_end(other.m_end) // other의 멤버 변수가 생성 및 초기화를 동시에 하도록 생성자 초기화 리스트로 선언
	{
		other.m_begin = nullptr; // 옮김 당한 vector는 begin last end 전부 nullptr 처리를 함으로서 정보에 더이상 접근 불가능하게 만듦
		other.m_last = nullptr;
		other.m_end = nullptr;
	}

	// v2 = std::move(v1);
	Vector& operator=(Vector&& other) noexcept // 단순 주소값 대입 및 nullptr 대입, 해당 함수 내부에서는 절대 예외가 터질 일 없으니 컴파일러에게 예외 검토하지 말라고 하는 것, 호출 스택 unwinding 코드 생성하지 않음=> binary 크기 줄어듦 && 실행 속도 빨라짐
	{
		if (this != &other) // 위 복사 생성자는 둘이 같은 객체일 리가 없다. 같은 객체라면 pass
		{
			// this->~Vector(); // 덮어쓰기이므로, 기존 정보는 아무도 가리키지 않게 되어 메모리 누수가 나니 미리 소멸자로 정리해준다
			// 그러나 생성이 없는 곳에서 사용자 몰래 소멸자가 불리는 건 side effect 생성 가능. 아래 swap으로 대체

			//m_begin = other.m_begin; // other의 포인터들도 초기화해준다
			//m_last = other.m_last;
			//m_end = other.m_end;


			//other.m_begin = nullptr; // double free 버그를 방지하기 위해 other의 정보를 지운다
			//other.m_last = nullptr;
			//other.m_end = nullptr;

			std::unique_lock<std::shared_mutex> lock(m_mutex);

			std::swap(m_begin, other.m_begin);
			std::swap(m_last, other.m_last);
			std::swap(m_end, other.m_end);
		}
		return *this; // 객체 반환
	}

	// bool return은 오류가 없이 넘어갈 수 있지만, 사실 메모리 할당이 false가 되면 버그고, void로 bad alloc 시키는 게 사용자가 이상을 명시적으로 알 수 있으니 더 좋은 선택 같다
	void reserve(size_type newSize) // stl vector의 경우 newSize 개수만큼 추가로 더 메모리 할당을 하지만, 해당 vector의 경우 newSize로 메모리 할당하게 구현 + 현재 크기보다 더 작으면 무시 / 추후 stl처럼 수정 예정
	{ 
		std::unique_lock<std::shared_mutex> lock(m_mutex);
		if (newSize <= capacity_internal()) return;
		growth_internal(newSize); 
	} 

	void resize(size_type newSize) // reserve와 resize의 다른 점은, reserve는 주어진 곳에 메모리 할당만 하는 반면, resize는 각 메모리 주소마다 특정 값으로 객체 생성 및 초기화를 진행한다
	{
		std::unique_lock<std::shared_mutex> lock(m_mutex);
		size_type curSize = size_internal(); // 현재 개수를 가져와서

		if (newSize > curSize) // 요청한 개수가 현재 개수보다 크면
		{
			growth_internal(newSize); // 벡터 크기를 키움
			
			for (size_type i = curSize; i < newSize; i++)
			{
				new (m_begin + i) T(); // 새로 추가되는 객체들을 생성
			}
		}
		else if (newSize < curSize) // 요청한 개수가 현재 개수보다 작으면
		{
			for (size_type i = newSize; i < curSize; i++) 
			{
				m_begin[i].~T(); // 그만큼 객체들을 소멸시킨다
			}
		}

		m_last = m_begin + newSize; // m_last는 newSize*sizeof(T)

		return;
	}

	void push_back(const T& value) // value는 수정되지 않고, &로 얕은 복사로 가져와서 복사 오버헤드 줄임(L-value)
	{
		std::unique_lock<std::shared_mutex> lock(m_mutex);
		push_back_internal();

		new (m_last) T(value); // m_last에 value 객체 생성 및 할당
		++m_last;
	}

	// 위와 동일한 코드
	void push_back(T&& value) // 이동 연산자(xvalue) 혹은 임시 객체가 add 사용할 때(R-value)
	{
		std::unique_lock<std::shared_mutex> lock(m_mutex);
		push_back_internal();

		new (m_last) T(std::move(value)); // 생성할 때 이동 연산을 한다
		++m_last;
	}

	T& operator[](size_type pos) { return m_begin[pos]; } // arr[i]를 읽게 해주는 operator
	
	const T& operator[](size_type pos) const { return m_begin[pos]; } // 해당 vector가 const인 경우에는 해당 함수가 불린다

	// T operator[](size_type pos) { return m_begin[pos]; } // 복사 불가는 바로 assert

	// T& 반환하는 대신, 해당 함수 안에서 수정하라
	void update(size_type pos, const T& newValue)
	{
		std::unique_lock<std::shared_mutex> lock(m_mutex);

		if (pos < size_internal()) { m_begin[pos] = newValue; }
	}

	// 이동 오버로드
	void update(size_type pos, T&& newValue)
	{
		std::unique_lock<std::shared_mutex> lock(m_mutex);

		if (pos < size_internal()) { m_begin[pos] = std::move(newValue); }
	}

	size_type size() const // 현재 vector에 들어있는 아이템의 총 개수
	{ 
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		return size_internal();
	}
	
	size_type capacity() const // 현재 vector에 들어갈 수 있는 총 개수
	{ 
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		return capacity_internal();
	} 
	size_type max_size() const { return (std::numeric_limits<size_type>::max)() / sizeof(T);} // 현재 시스템에서 가장 크게 들어갈 수 있는 개수
	// numeric_limits는 해당 변수의 최댓값을 가져온다, size type은 시스템마다 시스템 주소의 최댓값을 불러오므로

private:
	mutable std::shared_mutex m_mutex;

	T* m_begin = nullptr; // start data address
	T* m_last = nullptr;  // end data + 1(next data address)
	T* m_end = nullptr;	// end address of allocated memory

	float m_growthFactor = 1.5f; // 한 번 확장할 때 확장되는 정도, 


	void push_back_internal()
	{
		if (m_last >= m_end) {

			size_type curCap = capacity_internal();
			size_type nextCap = (curCap == 0) ? 1 : static_cast<size_type>(curCap * m_growthFactor);
			if (nextCap <= curCap) nextCap = curCap + 1;

			growth_internal(nextCap);
		}
	}

	void growth_internal(size_type newSize) // bad alloc을 넘기는 걸로 수정
	{
		if (newSize <= capacity_internal()) return; // growth하는 정도가 현재 용량보다 적을 때 return

		size_type finalSize = (newSize > max_size()) ? max_size() : newSize;

		T* newMemory = static_cast<T*>(operator new(finalSize * sizeof(T))); // bad alloc 주의
		// static cast를 쓴 이유: void*가 반환, CPP에서는 void*r 타입 캐스팅 불가능, static cast를 통해 해당 type으로 명시적 캐스팅 해야함.
		// operactor new를 통해 새 용량 메모리 할당 

		size_type curSize = size_internal();

		for (size_type i = 0; i < curSize; i++)
		{
			new (newMemory + i) T(std::move_if_noexcept(m_begin[i])); // 한 객체가 해당 주소로 이사 가기 때문에 move, 만약 이동 생성자 / 복사 생성자
		}

		for (size_type i = 0; i < curSize; i++)
		{
			m_begin[i].~T(); // 기존 주소의 객체는 소멸시킨다, 이동 생성 도중 예외 발생 시 이전 자료가 훼손되는 걸 막기 위해 생성과 소멸 단계 분리
		}

		operator delete(m_begin); // m_begin 포인터에 할당된 전체 메모리를 회수한다		

		m_begin = newMemory;
		m_last = newMemory + curSize; // 주의: 시작 + 현재 개수 = zero base라 마지막 요소 + 1인 주소를 할당 받음
		m_end = newMemory + finalSize;

		return;
	}

	size_type size_internal() const { return m_last - m_begin; }
	size_type capacity_internal() const { return m_end - m_begin; } // 현재 vector에 들어갈 수 있는 총 개수
};