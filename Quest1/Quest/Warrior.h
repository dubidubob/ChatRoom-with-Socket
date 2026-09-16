#pragma once
#include "Character.h"

class Warrior : public Character
{
public:
	using Character::Character;

	virtual void useSkill() override
	{
		std::cout << nameId << "이(가) 칼을 휘두릅니다!\n";
	}

	bool swingSword(Character& target, int damage, int logIdx, CharacterManager::LogData& outResult)
	{
		outResult.skill = SkillName::SwingSword;
		return executeSkill(target, damage, logIdx, outResult);
	}
};