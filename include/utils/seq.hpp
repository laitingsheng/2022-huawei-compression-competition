#ifndef __UTILS_SEQ_HPP__
#define __UTILS_SEQ_HPP__

#include <climits>
#include <cstddef>
#include <cstdint>

#include <concepts>
#include <stdexcept>
#include <utility>
#include <vector>

namespace utils::seq
{

namespace diff
{

template<std::integral T>
[[nodiscard]]
inline std::vector<T> construct(const std::vector<T> & data, T initial = 0)
{
	std::vector<T> sequence;
	sequence.reserve(data.size());

	T value = initial;
	for (T e : data)
	{
		sequence.push_back(e - value);
		value = e;
	}

	return sequence;
}

template<std::integral T>
[[nodiscard]]
inline std::vector<T> reconstruct(const std::vector<T> & sequence, T initial = 0)
{
	std::vector<T> data;
	data.reserve(sequence.size());

	T value = initial;
	for (T e : sequence)
	{
		value += e;
		data.push_back(value);
	}

	return data;
}

}

namespace vlq
{

template<std::unsigned_integral T>
[[nodiscard]]
inline std::vector<uint8_t> encode(const std::vector<T> & data)
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
inline std::vector<T> decode(const std::vector<uint8_t> & sequence, size_t count = 0)
{
	static_assert(sizeof(T) > 1, "decoding data with bytes only is useless");

	std::vector<T> re;
	re.reserve(count);

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
