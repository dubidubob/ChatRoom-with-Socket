# 01_Containers

`Vector`, `Unordered_map`을 직접 구현하고, 캐릭터 1000명이 턴마다 서로 공격하는 시뮬레이션에서 사용합니다.

## Vector
- 1.5배 성장, `move_if_noexcept`로 원소 이동
- 재할당 중 예외가 나면 새 메모리를 정리하고 기존 상태 유지 (strong guarantee)
- `push_back(v[0])`처럼 자기 원소를 넣는 경우 재할당 전에 복사

## Unordered_map
- separate chaining (`Vector<std::list<pair>>`), load factor 1.0, 버킷 2배 성장
- rehash는 `list::splice`로 노드를 옮김 → 할당 없음, `find()`가 준 포인터가 rehash 후에도 유효
- 새 버킷 배열을 만든 뒤 교체하므로 rehash 중 할당이 실패해도 기존 맵 유지

## 스레드 모델
컨테이너 호출 하나하나는 `shared_mutex`로 보호하지만, `operator[]`는 참조를 반환하므로 그것만으로는 충분하지 않습니다.
그래서 턴을 두 구간으로 나눴습니다.

1. 병렬 공격 — 코어 수만큼 스레드, 컨테이너는 읽기만
2. 사망자 정리 — 단일 스레드에서만 수정

캐릭터 HP는 캐릭터별 mutex, 생존 여부는 `atomic<bool>`. ThreadSanitizer로 경고 없음을 확인했습니다.

## 테스트 / 벤치마크
```
cd Tests
g++ -std=c++20 -g -fsanitize=address,undefined -I../Quest ContainerTests.cpp && ./a.out
g++ -std=c++20 -g -fsanitize=thread -I../Quest ContainerTests.cpp && ./a.out
g++ -std=c++20 -O2 -I../Quest Benchmark.cpp && ./a.out
```

삽입 시간 (g++ 13, -O2)

| N | Unordered_map | std::unordered_map |
|---:|---:|---:|
| 16,000 | 2.6 ms | 0.6 ms |
| 256,000 | 43 ms | 12 ms |

std보다 느린 건 호출마다 잠금을 잡기 때문입니다. 단일 스레드 용도라면 잠금을 정책 템플릿으로 분리하는 게 다음 개선점입니다.

> `Quest.vcxproj`는 v145(VS 2026) 툴셋입니다. VS 2022에서는 솔루션 대상 변경으로 v143을 선택하세요.
