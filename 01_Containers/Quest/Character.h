#pragma once
#include <atomic>
#include <iostream>
#include <mutex>
#include <string>

#include "CharacterManager.h" // LogData 정의 (중앙 로그 구조체)
#include "Enums.h"

class Character
{
public:
	int index;
	std::string name;
	std::string nameId; // 생성 후 불변
	int damage;
	CharacterManager* characterManager;

	Character(std::string inName, int inIndex, CharacterManager* manager)
		: index(inIndex),
		  name(std::move(inName)),
		  nameId(name + std::to_string(index)),
		  damage(0),
		  characterManager(manager)
	{
	}

	virtual ~Character() = default;

	void introduce() const { std::cout << "저는 " << nameId << "입니다.\n"; }

	bool isAlive() const { return m_isAlive.load(std::memory_order_acquire); }

	// 남은 HP를 잠금 안에서 함께 반환한다. 잠금 밖에서 hp를 다시 읽으면 다른 공격이 끼어들 수 있다.
	DamageResult takeDamage(int dmg, int& outRemainingHp)
	{
		std::lock_guard lock(m_mutex);
		if (!m_isAlive.load(std::memory_order_relaxed))
		{
			outRemainingHp = 0;
			return DamageResult::Missed;
		}

		m_hp -= dmg;
		if (m_hp <= 0)
		{
			m_hp = 0;
			outRemainingHp = 0;
			m_isAlive.store(false, std::memory_order_release);
			return DamageResult::Killed;
		}
		outRemainingHp = m_hp;
		return DamageResult::Hit;
	}

	virtual void useSkill() = 0;

protected:
	bool executeSkill(Character& target, int dmg, CharacterManager::LogData& outResult)
	{
		if (!isAlive())
		{
			return false;
		}

		int remainingHp = 0;
		const DamageResult res = target.takeDamage(dmg, remainingHp);
		if (res == DamageResult::Missed)
		{
			return false; // 이미 죽은 대상이면 다음 대상으로
		}

		outResult.attackerId = nameId;
		outResult.targetId = target.nameId;
		outResult.damage = dmg;
		outResult.targetHp = remainingHp;
		outResult.result = res;
		return true;
	}

private:
	int m_hp = 200;                     // m_mutex
	std::atomic<bool> m_isAlive{true};  // 쓰기는 m_mutex 안, 읽기는 lock-free
	std::mutex m_mutex;
};
