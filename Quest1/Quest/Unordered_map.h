#pragma once
#include <list>
#include <limits>
#include <utility> //std::pair
#include <iostream>
#include "Vector.h"

template<typename T1, typename T2>
class Unordered_map
{
public:
	using size_type = std::size_t;

	void reserve(size_type newCap) // 이 unordered map은 현재 vector의 전체 개수를 newCap으로 정의. 추후 변경 예정.
	{
		std::unique_lock<std::shared_mutex> lock(m_mutex);
		growth_internal(newCap); // 내부에서도 reserve 동작이 필요할 때(insert)가 있으므로 로직은 private 함수로 둔다
	}

	T2* find(const T1& Key)
	// return은 value가 아닌 value의 포인터로 하는 이유는? nullptr 반환을 위해 + 복사 생성자 비용 + 수정 가능성
	{
		if (m_buckets.size() == 0) return nullptr;

		size_type hashIndex = getHashIndex_internal(Key); // Key를 Hash로 변환해 search
		for (auto& pair : m_buckets[hashIndex])
		{
			if (pair.first == Key) return &pair.second;
		}

		return nullptr;
	}

	const T2* find(const T1& Key) const // 수정 불가하게 검색
	{
		if (m_buckets.size() == 0) return nullptr;

		size_type hashIndex = getHashIndex_internal(Key);
		for (auto& pair : m_buckets[hashIndex])
		{
			if (pair.first == Key) return &pair.second;
		}

		return nullptr;
	}

	/*수정 사항: [growth -> 객체 생성 -> 객체 삽입]을 [객체 생성 -> growth -> 객체 삽입]으로 변경
	* unordered map에서 growth는 vector처럼 실제 메모리 할당 공간이 적어서가 아닌, chaining O(n)에 가까워지기 때문
	* 즉 growth에 실패해도 에러가 나지 않는다
	* 그러나 객체 생성의 경우 생성자 단계에서 에러가 나는 등의 에러 가능성이 있기 때문에
	* 우선적으로 객체를 생성 하고, 이후 growth를 실행.
	* growth에 실패해도 객체는 잘 자료구조에 저장이 된다.
	* hashing을 두 번 해야한다는 단점이 있으나, insert fail을 최대한 보장하기 위해 순서 변경
	* (반면 객체가 이후에 생성된다면 growth에 실패한다면 insert는 fail 해버린다.)
	*/
	void insert(const T1& Key, const T2& Value)
	{
		std::unique_lock<std::shared_mutex> lock(m_mutex);

		if (m_buckets.size() == 0) { growth_internal(8); } /*
		* 메모리 할당이 안 됐을 때의 방어 코드. 
		* stl에서 잡은 8은 L1 캐시 8*8byte랑 같아지기 때문. 해시 맵 전부 L1 캐시에 넣어 캐시 미스 방지
		* 이 unordered map은 객체 list를 썼으므로 그냥 관례 + 1로 growth하는 vector와 달리 여기는 bucket rehashing 비용이 있기 때문에 1에서 8로 키움
		*/ 

		size_type curHashIndex = getHashIndex_internal(Key); // hashing 함수 최소화(변수)

		for (auto& pair : m_buckets[curHashIndex]) // list iterator 호출
		{
			if (pair.first == Key) return; // 이미 있다면 return한다. overwrite 안 하는 이유는 insert는 "새로운 값을 넣는다"라는 의미기 때문
		}

		m_buckets[curHashIndex].push_back({ Key, Value }); // 미리 객체 생성
		m_size++;

		size_type borderSize = static_cast<size_type>(static_cast<float>(m_buckets.size())* m_maxLoadFactor) + 1; // 소수점 올림
		if (m_size >= borderSize) // 만약 bucket 개수 * loadfactor보다 실제 개수가 크다면 growth 시도
		{
			size_type newSize = borderSize;
			growth_internal(newSize);
		}
	}

	void erase(const T1& Key)
	{
		std::unique_lock<std::shared_mutex> lock(m_mutex);

		if (m_buckets.size() == 0) return; // 지울 것도 없으니 return

		size_type hashIndex = getHashIndex_internal(Key);

		auto& targetList = m_buckets[hashIndex];

		for (auto it = targetList.begin(); it != targetList.end(); it++) //list의 iterator 활용
		{
			if (it->first == Key) // key와 같으면
			{
				targetList.erase(it); // 삭제한다. prev next 노드 연결

				m_size--; // insert와 erase가 증감 짝 / 둘만 수정 가능
				return;
			}
		}
	}

	void printDebugMap() //단순 개발용 debug 용도, 실제 Unordered_map에서는 없어야함
	{
		std::cout << "=== Unordered_map Debug Info ===\n";
		std::cout << "Total Elements (size): " << m_size << "\n";
		std::cout << "Bucket Array Size (capacity): " << m_buckets.size() << "\n";
		std::cout << "Current Load Factor: " << (float)m_size / (m_buckets.size() == 0 ? 1 : m_buckets.size()) << "\n";
		std::cout << "--------------------------------\n";

		for (size_type i = 0; i < m_buckets.size(); i++)
		{
			std::cout << "Bucket [" << i << "] (Chain size: " << m_buckets[i].size() << "): ";
			for (const auto& pair : m_buckets[i])
			{
				std::cout << "{" << pair.first << ":" << pair.second << "} -> ";
			}
			std::cout << "NULL\n";
		}
		std::cout << "================================\n";
	}

	size_type size() const { return m_size; } //m_size는 절대 바뀌면 안 된다는 거 명시
	size_type max_size() { return (std::numeric_limits<size_type>::max)() / sizeof(std::list<std::pair<const T1, T2>>); } // size type은 주소 공간의 크기 -> size type 최댓값 == 운영체제 최댓값

private:
	mutable std::shared_mutex m_mutex;

	// unordered map은 hash를 통해 O(1) 접근이 가능한 해시 맵 -> vector<hash>로 접근한다
	// 이때 vector의 index가 hash, 각 노드의 list로 선언해 open addrs보다 구현이 간단한 chaining 방식으로 사용.
	Vector<std::list<std::pair<const T1, T2>>> m_buckets;

	float m_maxLoadFactor = 1.0f; // 1 이상이면 무조건 겹치는 bucket이 생기기 때문
	int m_growthFactor = 2;	// 1.5 vector에 비해 큼. rehashing 비용 고려.
	size_type m_size = 0; //요소 개수

	size_type getHashIndex_internal(const T1& Key) const // hash 계산 함수
	{
		std::hash<T1> hasher; // 해당 T1에 대해 _Hash_representation 호출
		size_type hash = hasher(Key); // 객체를 연산해 size type으로 변환 -> 데이터 자체에 주소값이 내재되어 있어야 한다. <-> rand(key뿐만 아니라 순서에도 영향을 받음)
		size_type bucketSize = m_buckets.size() == 0 ? 1 : m_buckets.size(); // zero divide 피하기
		return hash % bucketSize; // 현재는 모듈로 편향 감수. 추후 개선
	}

	void growth_internal(size_type newCap) // 실제 용량 증가 함수
	{
		size_type finalSize = (newCap > max_size()) ? max_size() : newCap; // 운영체제의 캡 보장

		// Rehash
		Vector<std::list<std::pair<const T1, T2>>> prevBuckets = std::move(m_buckets); // 원본 저장
		
		m_buckets.resize(finalSize); // unorderedmap은 vector의 reserve 사용금지, 무조건 list 객체가 있어야 pushback 가능하기 때문

		for (size_type i = 0; i < prevBuckets.size(); i++)
		{
			auto& oldList = prevBuckets[i];
			for (auto it = oldList.begin(); it != oldList.end();)
			{
				size_type hashIndex = getHashIndex_internal(it->first); // rehashing

				auto targetIt = it++; //it는 splice 실행하면 preBucket에 소속되지 않으므로 splice 전에 ++
				m_buckets[hashIndex].splice(m_buckets[hashIndex].end(), oldList, targetIt); // oldList에서 노드 떼어내 새 list의 마지막에 노드 붙이는 splice
			}
		}

		return;
	}
};