#pragma once
#include "Character.h"

class Archer : public Character
{
public:
	using Character::Character;

	virtual void useSkill() override
	{
		std::cout << nameId << "이(가) 활을 쏩니다!\n";
	}

	bool shootArrow(Character& target, int damage, int logIdx, CharacterManager::LogData& outResult)
	{
		outResult.skill = SkillName::ShootArrow;
		return executeSkill(target, damage, logIdx, outResult);
	}
};