#pragma once
#include <cassert>
#include <cstddef>
#include <limits>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <utility>

// 동적 배열.
//
// 스레드 안전성: 각 멤버 함수 호출은 shared_mutex로 원자적이다.
// 단, operator[]가 반환한 참조는 다음 재할당까지만 유효하므로
// 병렬 구간에서는 읽기만 하고 수정은 단일 스레드 구간에서 한다. (Quest.cpp 턴 루프)
template <typename T>
class Vector
{
public:
	using size_type = std::size_t;

	Vector() = default;

	~Vector()
	{
		destroy_range(m_begin, m_last);
		::operator delete(m_begin);
	}

	Vector(const Vector&) = delete;
	Vector& operator=(const Vector&) = delete;

	Vector(Vector&& other) noexcept
		: m_begin(other.m_begin), m_last(other.m_last), m_end(other.m_end)
	{
		other.m_begin = other.m_last = other.m_end = nullptr;
	}

	Vector& operator=(Vector&& other) noexcept
	{
		if (this != &other)
		{
			std::scoped_lock lock(m_mutex, other.m_mutex);
			std::swap(m_begin, other.m_begin);
			std::swap(m_last, other.m_last);
			std::swap(m_end, other.m_end);
		}
		return *this;
	}

	void reserve(size_type newCapacity)
	{
		std::unique_lock lock(m_mutex);
		if (newCapacity > capacity_internal())
		{
			growth_internal(newCapacity);
		}
	}

	void resize(size_type newSize)
	{
		std::unique_lock lock(m_mutex);
		const size_type curSize = size_internal();

		if (newSize > curSize)
		{
			if (newSize > capacity_internal())
			{
				growth_internal(newSize);
			}
			T* cursor = m_last;
			try
			{
				for (; cursor != m_begin + newSize; ++cursor)
				{
					::new (static_cast<void*>(cursor)) T();
				}
			}
			catch (...)
			{
				destroy_range(m_last, cursor);
				throw;
			}
		}
		else
		{
			destroy_range(m_begin + newSize, m_last);
		}
		m_last = m_begin + newSize;
	}

	void push_back(const T& value)
	{
		std::unique_lock lock(m_mutex);
		if (m_last == m_end)
		{
			// value가 자기 원소일 수 있으므로(v.push_back(v[0])) 재할당 전에 복사해 둔다.
			T copy(value);
			growth_internal(next_capacity());
			::new (static_cast<void*>(m_last)) T(std::move(copy));
		}
		else
		{
			::new (static_cast<void*>(m_last)) T(value);
		}
		++m_last;
	}

	void push_back(T&& value)
	{
		std::unique_lock lock(m_mutex);
		if (m_last == m_end)
		{
			T moved(std::move(value));
			growth_internal(next_capacity());
			::new (static_cast<void*>(m_last)) T(std::move(moved));
		}
		else
		{
			::new (static_cast<void*>(m_last)) T(std::move(value));
		}
		++m_last;
	}

	T& operator[](size_type pos)
	{
		assert(pos < size_internal());
		return m_begin[pos];
	}

	const T& operator[](size_type pos) const
	{
		assert(pos < size_internal());
		return m_begin[pos];
	}

	void update(size_type pos, const T& newValue)
	{
		std::unique_lock lock(m_mutex);
		if (pos < size_internal())
		{
			m_begin[pos] = newValue;
		}
	}

	void update(size_type pos, T&& newValue)
	{
		std::unique_lock lock(m_mutex);
		if (pos < size_internal())
		{
			m_begin[pos] = std::move(newValue);
		}
	}

	size_type size() const
	{
		std::shared_lock lock(m_mutex);
		return size_internal();
	}

	bool empty() const { return size() == 0; }

	size_type capacity() const
	{
		std::shared_lock lock(m_mutex);
		return capacity_internal();
	}

	static constexpr size_type max_size()
	{
		return (std::numeric_limits<size_type>::max)() / sizeof(T);
	}

private:
	static constexpr double kGrowthFactor = 1.5;

	mutable std::shared_mutex m_mutex;
	T* m_begin = nullptr;
	T* m_last = nullptr; // 마지막 원소 다음
	T* m_end = nullptr;  // 할당 영역의 끝

	size_type size_internal() const { return static_cast<size_type>(m_last - m_begin); }
	size_type capacity_internal() const { return static_cast<size_type>(m_end - m_begin); }

	size_type next_capacity() const
	{
		const size_type cur = capacity_internal();
		const size_type next = (cur == 0) ? 1 : static_cast<size_type>(cur * kGrowthFactor);
		return next > cur ? next : cur + 1;
	}

	static void destroy_range(T* first, T* last) noexcept
	{
		for (; first != last; ++first)
		{
			first->~T();
		}
	}

	// 이동 중 예외가 나면 새 메모리를 정리하고 기존 상태를 유지한다. (strong guarantee)
	void growth_internal(size_type newCapacity)
	{
		if (newCapacity > max_size())
		{
			throw std::bad_array_new_length();
		}

		T* newMemory = static_cast<T*>(::operator new(newCapacity * sizeof(T)));
		const size_type curSize = size_internal();

		size_type constructed = 0;
		try
		{
			for (; constructed < curSize; ++constructed)
			{
				::new (static_cast<void*>(newMemory + constructed)) T(std::move_if_noexcept(m_begin[constructed]));
			}
		}
		catch (...)
		{
			destroy_range(newMemory, newMemory + constructed);
			::operator delete(newMemory);
			throw;
		}

		destroy_range(m_begin, m_last);
		::operator delete(m_begin);

		m_begin = newMemory;
		m_last = newMemory + curSize;
		m_end = newMemory + newCapacity;
	}
};
