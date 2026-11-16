// g++ -std=c++20 -O2 -I../Quest Benchmark.cpp
#include <chrono>
#include <iostream>
#include <unordered_map>
#include "Unordered_map.h"

using Clock = std::chrono::steady_clock;

int main()
{
	for (int n = 1000; n <= 256000; n *= 4)
	{
		auto t0 = Clock::now();
		Unordered_map<int, int> mine;
		for (int i = 0; i < n; ++i)
			mine.insert(i, i);

		auto t1 = Clock::now();
		std::unordered_map<int, int> ref;
		for (int i = 0; i < n; ++i)
			ref.emplace(i, i);
		auto t2 = Clock::now();

		std::chrono::duration<double, std::milli> a = t1 - t0, b = t2 - t1;
		std::cout << n << "\tmine " << a.count() << " ms\tstd " << b.count() << " ms\n";
	}
}
