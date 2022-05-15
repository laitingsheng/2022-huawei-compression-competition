#ifndef __UTILS_COUNTER_HPP__
#define __UTILS_COUNTER_HPP__

#include <cstddef>

#include <concepts>
#include <stdexcept>
#include <utility>
#include <vector>

#include <fmt/core.h>
#include <fmt/compile.h>

#include "./traits.hpp"

namespace utils
{

namespace counter
{

namespace differential
{

template<std::integral T, std::unsigned_integral SizeT>
class differential final
{
	T value, diff;
	SizeT count;
	std::vector<std::pair<T, SizeT>> counter;
public:
	differential() : value(0), diff(0), count(0), counter() {}

	differential(const differential &) = default;
	differential(differential &&) = default;

	void add(T new_value)
	{
		if (T current_diff = new_value - value; current_diff == diff)
			++count;
		else
		{
			if (count)
			[[likely]]
				counter.emplace_back(diff, count);
			diff = current_diff;
			count = 1;
		}
		value = new_value;
	}

	void commit()
	{
		if (count)
		[[likely]]
			counter.emplace_back(diff, count);
	}

	[[nodiscard]]
	[[using gnu : pure]]
	const auto & data() const
	{
		return counter;
	}

	[[nodiscard]]
	[[using gnu : pure]]
	auto size() const
	{
		return counter.size();
	}
};

template<std::integral T, std::unsigned_integral SizeT>
[[nodiscard]]
[[using gnu : always_inline]]
inline static size_t reconstruct(const char * read_pos, size_t read_size, std::vector<T> & vector)
{
	if (read_size < 4)
	[[unlikely]]
		throw std::runtime_error("source is too small");

	auto size = *reinterpret_cast<const SizeT *>(read_pos);
	read_pos += sizeof(SizeT);
	size_t read = sizeof(SizeT) + size * (sizeof(T) + sizeof(SizeT));
	if (read > read_size)
	[[unlikely]]
		throw std::runtime_error(fmt::format(FMT_STRING("need {} bytes to read, {} bytes left"), read, read_size));

	T value = 0;
	for (SizeT i = 0, index = 0; i < size; ++i)
	[[likely]]
	{
		auto diff = *reinterpret_cast<const T *>(read_pos);
		read_pos += sizeof(T);
		auto count = *reinterpret_cast<const SizeT *>(read_pos);
		read_pos += sizeof(SizeT);

		for (SizeT j = 0; j < count; ++j)
		[[likely]]
		{
			value += diff;
			vector[index++] = value;
		}
	}

	return read;
}

}

template<std::integral T, std::unsigned_integral SizeT>
[[nodiscard]]
[[using gnu : always_inline]]
inline static SizeT write(char * pos, size_t capacity, const std::vector<std::pair<T, SizeT>> & counter)
{
	size_t written = sizeof(SizeT) + counter.size() * (sizeof(T) + sizeof(SizeT));
	if (written > capacity)
	[[unlikely]]
		throw std::runtime_error("not enough space to serialise the counter");

	*reinterpret_cast<SizeT *>(pos) = counter.size();
	pos += sizeof(SizeT);

	for (const auto & [value, count] : counter)
	{
		*reinterpret_cast<T *>(pos) = value;
		pos += sizeof(T);
		*reinterpret_cast<SizeT *>(pos) = count;
		pos += sizeof(SizeT);
	}

	return written;
}

}

}

#endif
