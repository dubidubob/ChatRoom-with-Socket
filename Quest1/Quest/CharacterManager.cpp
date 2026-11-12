#include "CharacterManager.h"

#include "Character.h"
#include "Warrior.h" 
#include "Archer.h"
#include "Mage.h"

void CharacterManager::generateCharacter(int count)
{
    for (int i = 1; i <= count; i++)
    {
        int randNum = rand() % 3;
        switch (randNum)
        {
        case 0: createCharacter<Warrior>("워리어", i); break;
        case 1: createCharacter<Mage>("메이지", i); break;
        case 2: createCharacter<Archer>("아처", i); break;
        }
    }
}

Character* CharacterManager::findCharacter(const std::string& nameId)
{
	Character** ptr = m_aliveCharactersMap.find(nameId);
    if (ptr != nullptr) return *ptr;
    return nullptr;
}

void CharacterManager::removeDeadCharacters()
{
	for (int i = 0; i < m_aliveCharacters.size(); )
	{
		std::string key = m_aliveCharacters[i];
		Character** targetPtr = m_aliveCharactersMap.find(key);

		if (targetPtr == nullptr || !(*targetPtr)->isAlive())
		{
			m_aliveCharactersMap.erase(key);

			int lastIdx = m_aliveCharacters.size() - 1;
			m_aliveCharacters[i] = std::move(m_aliveCharacters[lastIdx]);
			m_aliveCharacters.resize(lastIdx);
		}
		else
		{
			i++;
		}
	}
}

void CharacterManager::printLog(const LogData& log)
{
	std::lock_guard<std::mutex> lock(g_logMutex);
	std::cout << log.attackerId << " → " << log.targetId
		<< " | 스킬: " << (int)log.skill
		<< " | 데미지: " << log.damage
		<< " | 남은HP: " << log.targetHp
		<< " | " << (log.result == DamageResult::Killed ? "사망" : "생존")
		<< "\n";
}
