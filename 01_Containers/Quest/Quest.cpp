#if defined(_MSC_VER) && defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <cstdint>
#include <thread>
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
    const int aliveNum = static_cast<int>(aliveNames.size());
    if (aliveNum <= 1) return;

    // shuffle
    Vector<int> aliveIdx;
    aliveIdx.resize(aliveNum);
    for (int i = 0; i < aliveNum; i++)
    {
        aliveIdx[i] = i;
    }

    // 엔진은 스레드마다 따로 (공유하면 잠금 필요)
    std::mt19937 curRand(randSeed);
    
    for (int i = aliveNum - 1; i > 0; i--)
    {
        std::uniform_int_distribution<int> dist(0, i); 
        
        int j = dist(curRand);

        // Fisher-Yates. aliveIdx는 스레드 지역 변수라 잠금 불필요
        std::swap(aliveIdx[i], aliveIdx[j]);
    }

    std::uniform_int_distribution<int> damageDist(1, 15);

    CharacterManager::LogData localLog;
    localLog.turn = manager->m_currentTurn;

    for (int i = 0; i < aliveNum; i++) // 살아있는 대상을 맞힐 때까지
    {
        int idx = aliveIdx[i];
        std::string key = aliveNames[idx];

        Character* target = manager->findCharacter(key);

        if (target != nullptr && target != attacker && target->isAlive())
        {
            int finalDamage = attacker->damage + damageDist(curRand);
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
            
        // 이번 턴 공격자 수집
        Vector<Character*> attackers;
        attackers.reserve(curAliveNum);
        for (int i = 0; i < curAliveNum; i++)
        {
            if (Character* attacker = manager.findCharacter(curAlive[i]))
            {
                attackers.push_back(attacker);
            }
        }

        std::random_device randDevice;
        std::mt19937 masterEngine(randDevice());
        Vector<uint32_t> seeds;
        seeds.resize(attackers.size());
        for (size_t i = 0; i < attackers.size(); i++)
        {
            seeds[i] = masterEngine();
        }

        manager.m_currentTurn++;

        // 병렬 공격: 컨테이너는 읽기만 한다. 스레드는 코어 수만큼 만들고 공격자를 나눠 맡긴다.
        const size_t workerCount = (std::max)(1u, std::thread::hardware_concurrency());
        const size_t attackerCount = attackers.size();
        Vector<std::thread> workers;
        workers.reserve(workerCount);
        for (size_t w = 0; w < workerCount && w < attackerCount; w++)
        {
            workers.push_back(std::thread([&, w]
            {
                for (size_t i = w; i < attackerCount; i += workerCount)
                {
                    SetTargetAndDamage(attackers[i], &manager, seeds[i]);
                }
            }));
        }
        for (size_t i = 0; i < workers.size(); i++)
        {
            workers[i].join();
        }

        // 정리: 단일 스레드에서만 컨테이너 수정
        manager.removeDeadCharacters();
        std::cout << "==================="<< manager.m_currentTurn <<"턴 완료. 남은 사람 수 : " << manager.getAliveCount() << "\n";
    }
}

int main()
{
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    Quest4();
}