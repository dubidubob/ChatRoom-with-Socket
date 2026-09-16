#pragma once
#include <string>
#include <iostream>
#include <mutex>

#include "Vector.h"
#include "CharacterManager.h" // Log Data 중앙화로 인해 CharacterManager를 알아야함.
#include "Enums.h"

class Character
{
public:
	int index;
	std::string name;
	std::string nameId;
	
	int hp;
	int damage;

	Vector<int> LogIndex;
	CharacterManager* characterManager;

	virtual ~Character() {}

	Character(std::string inName, int inIndex, CharacterManager* characterM) :
		hp(200),
		index(inIndex),
		name(inName),
		nameId(name + std::to_string(index)),
		damage(0),
		bIsAlive(true),
		characterManager(characterM)
	{
		// TODO: reserve index 하는 정도 수정, 
		LogIndex.reserve(50);
	}

	void introduce() { std::cout << "저는 " << nameId << "입니다.\n"; }

	bool isAlive() { return bIsAlive; }

	void printLog()
	{
		// characterManager의 LogVector 받아서 출력
		if (characterManager == nullptr) return;
		std::cout << "[" << nameId << " 로그] : ";
		for (int i = 0; i < LogIndex.size(); i++)
		{
			characterManager->printSingleLog(LogIndex[i]);
		}
	}

	DamageResult takeDamage(int dmg, int logIdx)
	{
		std::lock_guard<std::mutex> lock(m_mtx);

		if (!bIsAlive) return DamageResult::Missed;

		hp -= dmg;
		if (hp <= 0)
		{
			bIsAlive = false;
			hp = 0;
			return DamageResult::Killed;
		}

		return DamageResult::Hit;
	}

	virtual void useSkill() = 0;

	// legacy : virtual 함수로 useskill 불렀던 것, TODO: dynamic cast를 skill name 때 쓰고, 기본으로 useskill 호출하기?
	// virtual bool useSkill(Character& target, int dmg, int logIdx, CharacterManager::LogData& outResult) = 0;

protected:
	bool bIsAlive;
	std::mutex m_mtx;

	bool executeSkill(Character& target, int dmg, int logIdx, CharacterManager::LogData& outResult)
	{
		if (!bIsAlive) return true;
		
		DamageResult res = target.takeDamage(dmg, logIdx);

		// miss일 경우 로그 안 남기고 재시도
		if (res == DamageResult::Missed) { return false; }
		
		// skill은 위 스킬 함수에서 채워짐 => TODO: outResult가 한번에 완성되게 하는 게 책임이 깔끔할 듯
		outResult.attackerId = nameId;
		outResult.targetId = target.nameId;
		outResult.damage = dmg;
		outResult.targetHp = target.hp;
		outResult.result = res;

		// 객체에 공격한 로그 인덱스 기록 // TODO: 공격, 피해 로그 분리
		addLogIndex(logIdx);
		target.addLogIndex(logIdx);

		return true;
	}

	void addLogIndex(int idx)
	{
		std::lock_guard<std::mutex> lock(m_mtx);
		LogIndex.push_back(idx);
	}
};