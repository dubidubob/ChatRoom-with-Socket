#pragma once
#include <functional>
#include <iostream>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include "Vector.h"

// separate chaining 해시맵.
//
// rehash는 std::list::splice로 노드를 옮기므로 할당이 없고 노드 주소도 유지된다.
// 그래서 find()가 반환한 포인터는 rehash 후에도 erase 전까지 유효하다.
template <typename K, typename V, typename Hash = std::hash<K>>
class Unordered_map
{
public:
	using size_type = std::size_t;

	void reserve(size_type bucketCount)
	{
		std::unique_lock lock(m_mutex);
		if (bucketCount > m_buckets.size())
		{
			rehash_internal(bucketCount);
		}
	}

	V* find(const K& key)
	{
		std::shared_lock lock(m_mutex);
		return find_internal(key);
	}

	const V* find(const K& key) const
	{
		std::shared_lock lock(m_mutex);
		return find_internal(key);
	}

	// 키가 이미 있으면 덮어쓰지 않고 false.
	bool insert(const K& key, const V& value)
	{
		std::unique_lock lock(m_mutex);
		if (m_buckets.size() == 0)
		{
			rehash_internal(kInitialBucketCount);
		}

		auto& bucket = m_buckets[bucket_index(key)];
		for (const auto& entry : bucket)
		{
			if (entry.first == key)
			{
				return false;
			}
		}

		bucket.emplace_back(key, value);
		++m_size;

		if (static_cast<double>(m_size) > static_cast<double>(m_buckets.size()) * kMaxLoadFactor)
		{
			try
			{
				rehash_internal(m_buckets.size() * kGrowthFactor);
			}
			catch (const std::bad_alloc&)
			{
				// 삽입은 이미 끝났고 맵도 유효하다. 체인이 길어질 뿐이라 무시한다.
			}
		}
		return true;
	}

	bool erase(const K& key)
	{
		std::unique_lock lock(m_mutex);
		if (m_buckets.size() == 0)
		{
			return false;
		}

		auto& bucket = m_buckets[bucket_index(key)];
		for (auto it = bucket.begin(); it != bucket.end(); ++it)
		{
			if (it->first == key)
			{
				bucket.erase(it);
				--m_size;
				return true;
			}
		}
		return false;
	}

	size_type size() const
	{
		std::shared_lock lock(m_mutex);
		return m_size;
	}

	size_type bucket_count() const
	{
		std::shared_lock lock(m_mutex);
		return m_buckets.size();
	}

	void printDebugMap() const
	{
		std::shared_lock lock(m_mutex);
		std::cout << "size=" << m_size << " buckets=" << m_buckets.size() << "\n";
		for (size_type i = 0; i < m_buckets.size(); ++i)
		{
			std::cout << "[" << i << "] ";
			for (const auto& entry : m_buckets[i])
			{
				std::cout << "{" << entry.first << ":" << entry.second << "} ";
			}
			std::cout << "\n";
		}
	}

private:
	using Bucket = std::list<std::pair<const K, V>>;

	static constexpr size_type kInitialBucketCount = 8;
	static constexpr double kMaxLoadFactor = 1.0;
	static constexpr size_type kGrowthFactor = 2; // rehash 비용이 커서 Vector(1.5)보다 크게

	mutable std::shared_mutex m_mutex;
	Vector<Bucket> m_buckets;
	size_type m_size = 0;
	Hash m_hasher;

	size_type bucket_index(const K& key) const
	{
		return m_hasher(key) % m_buckets.size();
	}

	V* find_internal(const K& key) const
	{
		if (m_size == 0)
		{
			return nullptr;
		}
		auto& bucket = const_cast<Bucket&>(m_buckets[bucket_index(key)]);
		for (auto& entry : bucket)
		{
			if (entry.first == key)
			{
				return &entry.second;
			}
		}
		return nullptr;
	}

	// 새 버킷 배열을 먼저 만들고 노드를 옮긴 뒤 교체한다.
	// 버킷 할당이 실패해도 기존 맵은 그대로 남는다.
	void rehash_internal(size_type newBucketCount)
	{
		Vector<Bucket> next;
		next.resize(newBucketCount);

		for (size_type i = 0; i < m_buckets.size(); ++i)
		{
			Bucket& oldBucket = m_buckets[i];
			while (!oldBucket.empty())
			{
				const size_type index = m_hasher(oldBucket.front().first) % newBucketCount;
				next[index].splice(next[index].end(), oldBucket, oldBucket.begin());
			}
		}
		m_buckets = std::move(next);
	}
};
