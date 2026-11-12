#pragma once
#include "Vector.h"
#include "Unordered_map.h"
#include "Enums.h"

class Character;

class CharacterManager
{
public:
	struct LogData {
		std::string attackerId;	// 공격자 ID
		std::string targetId;	// 피해자 ID
		int damage;				// 데미지
		int targetHp;
		DamageResult result;	// 결과: 사망, 생존
		SkillName skill;		// 스킬 내용
	};

	CharacterManager() {}

	CharacterManager(int count)
	{
		m_allCharacters.reserve(count);
		m_aliveCharacters.reserve(count);

	}

	~CharacterManager() = default;

	int m_currentTurn = 0;

	// Character 조작 ===============================================================
	void generateCharacter(int count);

	Character* findCharacter(const std::string& nameId);

	void removeDeadCharacters();

	void setTargetCharacter();

	// Get 함수 ======================================================================
	const Vector<std::string>& getAliveCharacters() const { return m_aliveCharacters; }
	
	int getAliveCount() const { return m_aliveCharacters.size(); }

	
	// Print a completed combat log immediately; serialize console output.
	void printLog(const LogData& log);

private:
	std::mutex g_logMutex;

	Vector<std::unique_ptr<Character>> m_allCharacters;		// 모든 객체
	Unordered_map<std::string, Character*> m_aliveCharactersMap; // 살아있는 객체의 포인터 맵(조회용)
	Vector<std::string> m_aliveCharacters;  // 살아있는 객체의 String(반복 순회용)


	template<typename T, typename... Args>
	void createCharacter(Args&&... args) 
	{
		auto characterPtr = std::make_unique<T>(std::forward<Args>(args)..., this); // 가변인자 통해 복사 비용 삭제

		m_aliveCharacters.push_back(characterPtr->nameId); // 비직관적이다. T면서 character일 걸 가정하고 로직이 돌아간다 > implicit interface라고 한다
		m_aliveCharactersMap.insert(characterPtr->nameId, characterPtr.get()); // {워리어14, 포인터}


		// print 생성 로그, TODO : std cout는 print log 같은 걸로 다른 곳에 따로 모아두기
		std::cout << characterPtr->nameId << " 생성!\n";

		m_allCharacters.push_back(std::move(characterPtr));
	}
};