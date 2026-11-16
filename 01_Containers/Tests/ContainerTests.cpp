// g++ -std=c++20 -g -fsanitize=address,undefined -I../Quest ContainerTests.cpp
// g++ -std=c++20 -g -fsanitize=thread -I../Quest ContainerTests.cpp
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "Vector.h"
#include "Unordered_map.h"

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_failures; } } while (0)

// n번째 복사에서 예외를 던진다. move가 noexcept가 아니라 재할당 때 복사 경로를 탄다.
struct Tracked
{
	static inline int alive = 0;
	static inline int copiesUntilThrow = -1; // -1이면 던지지 않음
	int value = 0;

	Tracked() { ++alive; }
	explicit Tracked(int v) : value(v) { ++alive; }
	Tracked(const Tracked& other) : value(other.value)
	{
		if (copiesUntilThrow == 0)
		{
			throw std::runtime_error("copy failed");
		}
		if (copiesUntilThrow > 0)
		{
			--copiesUntilThrow;
		}
		++alive;
	}
	Tracked(Tracked&& other) : value(other.value) { ++alive; }
	Tracked& operator=(const Tracked&) = default;
	~Tracked() { --alive; }
};

static void TestVectorBasics()
{
	Vector<int> v;
	for (int i = 0; i < 1000; ++i)
	{
		v.push_back(i);
	}
	CHECK(v.size() == 1000);
	CHECK(v.capacity() >= 1000);
	bool ordered = true;
	for (int i = 0; i < 1000; ++i)
	{
		ordered &= (v[i] == i);
	}
	CHECK(ordered);

	v.resize(10);
	CHECK(v.size() == 10 && v[9] == 9);
	v.resize(20);
	CHECK(v.size() == 20 && v[19] == 0);

	Vector<int> moved = std::move(v);
	CHECK(moved.size() == 20 && v.size() == 0);
}

static void TestVectorSelfReferencePushBack()
{
	Vector<std::string> v;
	v.push_back(std::string(64, 'a')); // SSO 회피
	for (int i = 0; i < 50; ++i)
	{
		v.push_back(v[0]);
	}
	bool allSame = true;
	for (size_t i = 0; i < v.size(); ++i)
	{
		allSame &= (v[i] == std::string(64, 'a'));
	}
	CHECK(allSame);
}

static void TestVectorStrongGuarantee()
{
	{
		Vector<Tracked> v;
		v.reserve(4);
		for (int i = 0; i < 4; ++i)
		{
			v.push_back(Tracked(i));
		}
		const int aliveBefore = Tracked::alive;

		Tracked::copiesUntilThrow = 2;
		bool threw = false;
		try
		{
			v.push_back(Tracked(99));
		}
		catch (const std::runtime_error&)
		{
			threw = true;
		}
		Tracked::copiesUntilThrow = -1;

		CHECK(threw);
		CHECK(v.size() == 4);
		CHECK(Tracked::alive == aliveBefore);
		bool intact = true;
		for (int i = 0; i < 4; ++i)
		{
			intact &= (v[i].value == i);
		}
		CHECK(intact);
	}
	CHECK(Tracked::alive == 0);
}

static void TestVectorMoveOnly()
{
	Vector<std::unique_ptr<int>> v;
	for (int i = 0; i < 100; ++i)
	{
		v.push_back(std::make_unique<int>(i));
	}
	CHECK(v.size() == 100 && *v[99] == 99);
}

static void TestMapBasics()
{
	Unordered_map<std::string, int> m;
	const int count = 100000;
	for (int i = 0; i < count; ++i)
	{
		m.insert("key" + std::to_string(i), i);
	}
	CHECK(m.size() == static_cast<size_t>(count));
	CHECK(m.bucket_count() >= m.size());

	CHECK(!m.insert("key5", -1));
	CHECK(*m.find("key5") == 5);
	CHECK(m.find("missing") == nullptr);

	bool allFound = true;
	for (int i = 0; i < count; ++i)
	{
		const int* value = m.find("key" + std::to_string(i));
		allFound &= (value && *value == i);
	}
	CHECK(allFound);

	CHECK(m.erase("key7"));
	CHECK(!m.erase("key7"));
	CHECK(m.find("key7") == nullptr);
	CHECK(m.size() == static_cast<size_t>(count - 1));
}

static void TestMapPointerStability()
{
	Unordered_map<int, int> m;
	m.insert(42, 4242);
	int* before = m.find(42);
	const size_t bucketsBefore = m.bucket_count();
	for (int i = 0; i < 10000; ++i)
	{
		m.insert(i + 1000, i);
	}
	CHECK(m.bucket_count() > bucketsBefore);
	CHECK(m.find(42) == before);
	CHECK(*before == 4242);
}

static void TestMapConcurrent()
{
	Unordered_map<int, int> m;
	constexpr int kThreads = 8;
	constexpr int kPerThread = 5000;
	std::vector<std::thread> threads;
	for (int t = 0; t < kThreads; ++t)
	{
		threads.emplace_back([&m, t]
		{
			for (int i = 0; i < kPerThread; ++i)
			{
				const int key = t * kPerThread + i;
				m.insert(key, key * 2);
				const int* value = m.find(key);
				if (!value || *value != key * 2)
				{
					std::printf("  FAIL concurrent find key=%d\n", key);
				}
			}
		});
	}
	for (auto& thread : threads)
	{
		thread.join();
	}
	CHECK(m.size() == static_cast<size_t>(kThreads * kPerThread));
}

int main()
{
	TestVectorBasics();
	TestVectorSelfReferencePushBack();
	TestVectorStrongGuarantee();
	TestVectorMoveOnly();
	TestMapBasics();
	TestMapPointerStability();
	TestMapConcurrent();
	std::printf(g_failures == 0 ? "ok\n" : "%d failed\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
