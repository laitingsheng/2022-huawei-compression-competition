#ifndef __UTILS_COUNTER_HPP__
#define __UTILS_COUNTER_HPP__

#include <cstddef>

#include <algorithm>
#include <concepts>
#include <stdexcept>
#include <utility>
#include <vector>

namespace utils::counter
{

template<std::integral T>
[[nodiscard]]
inline static size_t write(char * pos, size_t & capacity, const std::vector<T> & data)
{
	constexpr size_t cell_size = sizeof(T) + sizeof(size_t);

	T value = 0;
	size_t count = 0, total = 0;
	for (T e : data)
	{
		if (e == value)
			++count;
		else
		{
			if (count)
			{
				if (capacity < cell_size)
				[[unlikely]]
					throw std::runtime_error("insufficient capacity to write the counter");
				capacity -= cell_size;
				total += cell_size;

				*reinterpret_cast<T *>(pos) = value;
				pos += sizeof(T);;
				*reinterpret_cast<size_t *>(pos) = count;
				pos += sizeof(size_t);
			}
			value = e;
			count = 1;
		}
	}
	if (count)
	[[likely]]
	{
		if (capacity < cell_size)
		[[unlikely]]
			throw std::runtime_error("insufficient capacity to write the counter");
		capacity -= cell_size;
		total += cell_size;

		*reinterpret_cast<T *>(pos) = value;
		pos += sizeof(T);;
		*reinterpret_cast<size_t *>(pos) = count;
		pos += sizeof(size_t);
	}

	return total;
}

template<std::integral T>
[[nodiscard]]
inline static size_t read(const char * pos, size_t & size, std::vector<T> & data)
{
	constexpr size_t cell_size = sizeof(T) + sizeof(size_t);

	T value = 0;
	size_t count = 0, total = 0;

	for (T & e : data)
	{
		if (!count)
		{
			if (size < cell_size)
			[[unlikely]]
				throw std::runtime_error("insufficient size to read the counter");
			size -= cell_size;
			total += cell_size;

			value = *reinterpret_cast<const T *>(pos);
			pos += sizeof(T);
			count = *reinterpret_cast<const size_t *>(pos);
			pos += sizeof(size_t);

			if (!count)
			[[unlikely]]
				throw std::runtime_error("invalid counter");
		}

		e = value;
		--count;
	}

	if (count)
	[[unlikely]]
		throw std::runtime_error("corrupted counter");

	return total;
}

}

#endif
