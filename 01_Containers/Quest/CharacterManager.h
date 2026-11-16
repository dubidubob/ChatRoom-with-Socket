#pragma once
#include "Vector.h"
#include "Unordered_map.h"
#include "Enums.h"
#include <memory>
#include <mutex>
#include <string>

class Character;

class CharacterManager
{
public:
	struct LogData {
		int turn = 0;
		std::string attackerId;	// 공격자 ID
		std::string targetId;	// 피해자 ID
		int damage = 0;			// 데미지
		int targetHp = 0;		// 피해 직후 남은 HP
		DamageResult result = DamageResult::Missed;	// 결과: 사망, 생존
		SkillName skill = SkillName::CastSpell;		// 스킬 내용
	};

	CharacterManager() {}

	explicit CharacterManager(int count)
	{
		m_allCharacters.reserve(count);
		m_aliveCharacters.reserve(count);

	}

	~CharacterManager() = default;

	int m_currentTurn = 0; // 턴 사이에만 증가

	// Character 조작 ===============================================================
	void generateCharacter(int count);

	Character* findCharacter(const std::string& nameId);

	void removeDeadCharacters();

	void setTargetCharacter();

	// Get 함수 ======================================================================
	const Vector<std::string>& getAliveCharacters() const { return m_aliveCharacters; }
	
	int getAliveCount() const { return static_cast<int>(m_aliveCharacters.size()); }

	
	// 여러 스레드에서 호출되므로 출력을 직렬화한다.
	void printLog(const LogData& log);

private:
	std::mutex m_logMutex;

	Vector<std::unique_ptr<Character>> m_allCharacters;		// 모든 객체
	Unordered_map<std::string, Character*> m_aliveCharactersMap; // 살아있는 객체의 포인터 맵(조회용)
	Vector<std::string> m_aliveCharacters;  // 살아있는 객체의 String(반복 순회용)


	template<typename T, typename... Args>
	void createCharacter(Args&&... args) 
	{
		auto characterPtr = std::make_unique<T>(std::forward<Args>(args)..., this); // perfect forwarding

		m_aliveCharacters.push_back(characterPtr->nameId);
		m_aliveCharactersMap.insert(characterPtr->nameId, characterPtr.get());


		std::cout << characterPtr->nameId << " 생성!\n";

		m_allCharacters.push_back(std::move(characterPtr));
	}
};