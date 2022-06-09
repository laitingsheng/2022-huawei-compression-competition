#ifndef __UTILS_SEQ_HPP__
#define __UTILS_SEQ_HPP__

#include <climits>
#include <cstddef>
#include <cstdint>

#include <algorithm>
#include <concepts>
#include <stdexcept>
#include <utility>
#include <vector>

namespace utils::seq
{

namespace diff
{

template<std::integral T>
inline static std::vector<T> & construct(std::vector<T> & data, T initial = 0)
{
	T value = initial;
	for (T & e : data)
	{
		T old = e;
		e -= value;
		value = old;
	}

	return data;
}

template<std::integral T>
[[nodiscard]]
inline static std::vector<T> construct(const std::vector<T> & data, T initial = 0)
{
	std::vector<T> re(data);
	return std::move(construct(re, initial));
}

template<std::integral T>
[[nodiscard]]
inline static std::vector<T> construct(std::vector<T> && data, T initial = 0)
{
	std::vector<T> re(data);
	return std::move(construct(re, initial));
}

template<std::integral T>
inline static std::vector<T> & reconstruct(std::vector<T> & sequence, T initial = 0)
{
	T value = initial;
	for (T & e : sequence)
	{
		e += value;
		value = e;
	}

	return sequence;
}

template<std::integral T>
[[nodiscard]]
inline static std::vector<T> reconstruct(const std::vector<T> & sequence, T initial = 0)
{
	std::vector<T> re(sequence);
	return std::move(reconstruct(re, initial));
}

template<std::integral T>
[[nodiscard]]
inline static std::vector<T> reconstruct(std::vector<T> && sequence, T initial = 0)
{
	std::vector<T> re(sequence);
	return std::move(reconstruct(re, initial));
}

}

namespace compact
{

template<uint8_t bit_count>
inline static std::vector<uint8_t> & construct(std::vector<uint8_t> & data)
{
	static_assert(bit_count > 0 && bit_count <= 4, "bit_count must be in the range of [1, 4]");
	constexpr uint8_t compacted = CHAR_BIT / bit_count;

	size_t index = 0;
	uint8_t offset = 0;
	for (auto e : data)
	{
		if (offset < compacted)
		{
			data[index] = data[index] << bit_count | e;
			++offset;
		}
		else
		{
			data[++index] = e;
			offset = 1;
		}
	}

	data.shrink_to_fit();
	return data;
}

template<uint8_t bit_count>
[[nodiscard]]
inline static std::vector<uint8_t> construct(std::vector<uint8_t> data)
{
	return std::move(construct<bit_count>(data));
}

template<uint8_t bit_count>
inline static std::vector<uint8_t> & reconstruct(std::vector<uint8_t> & sequence, size_t count)
{
	static_assert(bit_count > 0 && bit_count <= 4, "bit_count must be in the range of [1, 4]");
	constexpr uint8_t compacted = CHAR_BIT / bit_count, mask = (1 << bit_count) - 1;

	if (!sequence.size() || !count)
		return sequence;

	if ((count - 1 + compacted) / compacted != sequence.size())
		throw std::runtime_error("invalid size");

	size_t index = sequence.size(), pos = count;
	sequence.resize(count);

	if (uint8_t remain = count & (compacted - 1); remain)
	{
		auto e = sequence[--index];
		while (remain--)
		{
			sequence[--pos] = e & mask;
			e >>= bit_count;
		}
	}

	while (index--)
	{
		auto e = sequence[index];
		for (uint8_t i = 0; i < compacted; ++i)
		{
			sequence[--pos] = e & mask;
			e >>= bit_count;
		}
	}

	return sequence;
}

}

namespace vlq
{

template<std::unsigned_integral T>
[[nodiscard]]
inline static std::vector<uint8_t> encode(const std::vector<T> & data)
{
	static_assert(sizeof(T) > 1, "encoding data with bytes only is useless");

	std::vector<uint8_t> re;
	re.reserve(data.size() * sizeof(T));

	constexpr uint8_t shift = CHAR_BIT - 1, indicator = 1 << shift, mask = indicator - 1;
	for (T e : data)
	{
		while (e > mask)
		{
			re.push_back((e & mask) | indicator);
			e >>= shift;
		}
		re.push_back(e);
	}

	re.shrink_to_fit();
	return re;
}

template<std::unsigned_integral T>
[[nodiscard]]
inline static std::vector<T> decode(const std::vector<uint8_t> & sequence, size_t count = SIZE_MAX)
{
	static_assert(sizeof(T) > 1, "decoding data with bytes only is useless");

	std::vector<T> re;
	re.reserve(std::min(sequence.size(), count));

	T value = 0;
	uint8_t shifts = 0;
	constexpr uint8_t shift = CHAR_BIT - 1, indicator = 1 << shift, mask = indicator - 1;
	for (auto byte : sequence)
	{
		if (byte & indicator)
		{
			value |= (byte & mask) << shifts;
			shifts += shift;
		}
		else
		{
			re.push_back(value | (byte << shifts));
			value = 0;
			shifts = 0;
		}
	}

	if (value != 0 || shifts != 0)
	[[unlikely]]
		throw std::runtime_error("incomplete vlq sequence");

	re.shrink_to_fit();
	return re;
}

}

}

#endif
