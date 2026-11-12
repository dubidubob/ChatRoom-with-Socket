#define _CRTDBG_MAP_ALLOC
#include <cstdlib>
#include <crtdbg.h>
#include <iostream>
#include <string>
#include <random>
#include <functional>  
#include <ctime>
#include <memory>

#include "Vector.h"
#include "Unordered_map.h"

#include "Warrior.h" // includes Character.h
#include "Archer.h"
#include "Mage.h"

#include "CharacterManager.h"

int g_testNum = 1000;
void SetTargetAndDamage(Character* attacker, CharacterManager* manager, uint32_t randSeed)
{
    if (!attacker->isAlive()) return;

    const Vector<std::string>& aliveNames = manager->getAliveCharacters();
    int aliveNum = aliveNames.size();
    if (aliveNum <= 1) return;

    // shuffle
    Vector<int> aliveIdx;
    aliveIdx.resize(aliveNum);
    for (int i = 0; i < aliveNum; i++)
    {
        aliveIdx.update(i, i); // aliveIdx[i] = i;
    }

    std::mt19937 curRand(randSeed); // 이런 random 함수는 결국 정수 배열 기반으로, 2.5kb의 상태 배열을 가진다. => L1 캐시 부담!
    // PcgEngine rand_engine(randSeed); 외부 라이브러리...
    
    for (int i = aliveNum - 1; i > 0; i--)
    {
        std::uniform_int_distribution<int> dist(0, i); 
        // 난수 생성과 분포를 분리한 이유: 다양한 분포 형태에 대응하기 위함 + 상태를 가지는 분포에 대한 복잡성 경감
        
        int j = dist(curRand);

        int tmpI = aliveIdx[i];
        int tmpJ = aliveIdx[j];

        aliveIdx.update(i, tmpI);
        aliveIdx.update(j, tmpJ);
    }

    std::uniform_int_distribution<int> damageDist(1, 15);

    CharacterManager::LogData localLog;

    for (int i = 0; i < aliveNum; i++) // 공격 성공할 때까지 반복, miss 시 localLog가 overwrite된다
    {
        int idx = aliveIdx[i];
        std::string key = aliveNames[idx];

        Character* target = manager->findCharacter(key);

        if (target != nullptr && target != attacker && target->isAlive())
        {
            int finalDamage = attacker->damage + damageDist(curRand);
            /*if (attacker->useSkill(*target, finalDamage, localLog))
            {
                break;
            }*/
            if (Mage* mage = dynamic_cast<Mage*>(attacker))
            {
                if (mage->castSpell(*target, finalDamage, localLog)) { manager->printLog(localLog); break; }
            }
            else if (Warrior* warrior = dynamic_cast<Warrior*>(attacker))
            {
                if (warrior->swingSword(*target, finalDamage, localLog)) { manager->printLog(localLog); break; }
            }
            else if (Archer* archer = dynamic_cast<Archer*>(attacker))
            {
                if (archer->shootArrow(*target, finalDamage, localLog)) { manager->printLog(localLog); break; }
            }
        }
    }


}

void Quest4()
{
    CharacterManager manager;
    manager.generateCharacter(g_testNum);

    while (true)
    {
        const Vector<std::string>& curAlive = manager.getAliveCharacters();
        int curAliveNum = manager.getAliveCount();

        if (curAliveNum == 0)
        {
            std::cout << "전멸했다.\n";
            break;
        }
        if (curAliveNum == 1)
        {
            std::cout << curAlive[0] << "은(는) 홀로 남았다.\n";
            break;
        }
            
        Vector<std::unique_ptr<std::thread>> attackers;
        attackers.reserve(curAliveNum);

        std::random_device randDevice;
        uint64_t masterSeed = randDevice();

        std::mt19937 masterEngine(masterSeed);

        manager.m_currentTurn++;

        for (int i = 0; i < curAliveNum; i++)
        {
            Character* attacker = manager.findCharacter(curAlive[i]);
            if (attacker != nullptr)
            {
                uint32_t workerSeed = masterEngine();

                attackers.push_back(std::make_unique<std::thread>(SetTargetAndDamage,
                    attacker, &manager, workerSeed));
            }
        }

        for (int i = 0; i < attackers.size(); i++)
        {
            if (attackers[i]->joinable())
            {
                attackers[i]->join();
            }
        }

        manager.removeDeadCharacters();
        std::cout << "==================="<< manager.m_currentTurn <<"턴 완료. 남은 사람 수 : " << manager.getAliveCount() << "\n";
    }
}

int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    Quest4();
}