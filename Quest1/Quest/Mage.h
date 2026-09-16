#pragma once
#include "Character.h"

class Mage : public Character
{
public:
	using Character::Character;

	virtual void useSkill() override
	{
		std::cout << nameId << "이(가) 마법을 시전합니다!\n";
	}

	bool castSpell(Character& target, int damage, int logIdx, CharacterManager::LogData& outResult)
	{
		outResult.skill = SkillName::CastSpell;
		return executeSkill(target, damage, logIdx, outResult);
	}
};