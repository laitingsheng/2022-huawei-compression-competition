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

}

#endif
