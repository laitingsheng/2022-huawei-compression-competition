#ifndef __HXV_HPP__
#define __HXV_HPP__

#include <cstdint>
#include <cstdio>

#include <vector>

#include <fmt/core.h>

#include "utils.hpp"

namespace hxv
{

template<typename T>
[[nodiscard]]
[[using gnu : always_inline, hot]]
inline static const char * from_hex_chars(const char * input, T & value)
{
	auto ptr = input;

	T re = 0;
	for (; ptr < input + 2 * sizeof(T); ++ptr)
	{
		char c = *ptr;
		re = (re << 4) + (c >= 'A' ? c - 'A' + 10 : c - '0');
	}
	value = re;

	return ptr;
}

template<typename T>
[[nodiscard]]
[[using gnu : always_inline, hot]]
inline static const char * from_hex_chars(const char * input, char sep, T & value)
{
	auto ptr = from_hex_chars(input, value);
	if (*ptr++ != sep)
	{
		fmt::print(stderr, "Separator mismatched\n");
		return nullptr;
	}
	return ptr;
}

[[nodiscard]]
[[using gnu : always_inline, hot]]
inline static const char * readline(
	const char * pos,
	char sep,
	uint8_t & ss,
	uint8_t & nn,
	uint16_t & yyyy,
	uint16_t & hhhn,
	uint16_t & nnww,
	uint16_t & wppp
)
{
	pos = from_hex_chars(pos, ss);
	if (!pos)
		return nullptr;

	pos = from_hex_chars(pos, sep, nn);
	if (!pos)
		return nullptr;

	pos = from_hex_chars(pos, sep, yyyy);
	if (!pos)
		return nullptr;

	pos = from_hex_chars(pos, sep, hhhn);
	if (!pos)
		return nullptr;

	pos = from_hex_chars(pos, sep, nnww);
	if (!pos)
		return nullptr;

	pos = from_hex_chars(pos, '\n', wppp);
	if (!pos)
		return nullptr;

	return pos;
}

[[nodiscard]]
[[using gnu : always_inline]]
inline int compress(
	const char * begin,
	const char * end,
	char sep,
	uint32_t & line_count,
	char * output,
	uint32_t capacity,
	uint32_t & written
)
{
	utils::counter::simple<uint8_t, uint32_t> ss_counter;
	utils::counter::differential<uint8_t, int16_t, uint32_t> nn_counter;
	std::vector<uint16_t> all_yyyy, all_hhhn, all_nnww, all_wppp;

	line_count = 0;
	for (auto pos = begin; pos < end;)
	{
		uint8_t ss, nn;
		uint16_t yyyy, hhhn, nnww, wppp;
		pos = readline(pos, sep, ss, nn, yyyy, hhhn, nnww, wppp);
		if (!pos)
			return 1;


		ss_counter.add(ss);
		nn_counter.add(nn);

		all_yyyy.push_back(yyyy);
		all_hhhn.push_back(hhhn);
		all_nnww.push_back(nnww);
		all_wppp.push_back(wppp);

		++line_count;
	}
	ss_counter.commit();
	nn_counter.commit();

	written = 0;

	auto write_pos = utils::counter::write(output, capacity, written, ss_counter.data());
	if (!write_pos)
		return 1;

	write_pos = utils::counter::write(write_pos, capacity, written, nn_counter.data());
	if (!write_pos)
		return 1;

	write_pos = utils::fl2::compress_vector(write_pos, capacity, written, all_yyyy);
	if (!write_pos)
		return 1;

	write_pos = utils::fl2::compress_vector(write_pos, capacity, written, all_hhhn);
	if (!write_pos)
		return 1;

	write_pos = utils::fl2::compress_vector(write_pos, capacity, written, all_nnww);
	if (!write_pos)
		return 1;

	write_pos = utils::fl2::compress_vector(write_pos, capacity, written, all_wppp);
	if (!write_pos)
		return 1;

	return 0;
}

template<typename T>
[[using gnu : always_inline, hot]]
inline static void to_hex_chars(T value, char * output)
{
	for (auto reverse = output + 2 * sizeof(T) - 1; reverse >= output; --reverse)
	{
		uint8_t digit = value & 0xF;
		*reverse = digit < 10 ? '0' + digit : 'A' + digit - 10;
		value >>= 4;
	}
}

[[nodiscard]]
[[using gnu : always_inline, hot]]
inline static const char * from_simple_counter(const char * read_pos, char * write_pos)
{
	auto size = *reinterpret_cast<const uint32_t *>(read_pos);
	read_pos += 4;

	for (uint32_t i = 0; i < size; ++i)
	{
		auto value = *reinterpret_cast<const uint8_t *>(read_pos);
		read_pos += 1;
		auto count = *reinterpret_cast<const uint32_t *>(read_pos);
		read_pos += 4;

		for (uint32_t j = 0; j < count; ++j)
		{
			to_hex_chars(value, write_pos);
			write_pos += 25;
		}
	}

	return read_pos;
}

[[nodiscard]]
[[using gnu : always_inline, hot]]
inline static const char * from_differential_counter(const char * read_pos, char * write_pos)
{
	auto size = *reinterpret_cast<const uint32_t *>(read_pos);
	read_pos += 4;

	uint8_t value = 0;
	for (uint32_t i = 0; i < size; ++i)
	{
		auto diff = *reinterpret_cast<const uint8_t *>(read_pos);
		read_pos += 1;
		auto count = *reinterpret_cast<const uint32_t *>(read_pos);
		read_pos += 4;

		for (uint32_t j = 0; j < count; ++j)
		{
			value += diff;
			to_hex_chars(value, write_pos);
			write_pos += 25;
		}
	}

	return read_pos;
}

[[using gnu : always_inline, hot]]
inline static void from_vector(const std::vector<uint16_t> & data, char sep, char * write_pos)
{
	for (uint32_t i = 0; i < data.size(); ++i)
	{
		to_hex_chars(data[i], write_pos);
		*(write_pos + 4) = sep;
		write_pos += 25;
	}
}

[[using gnu : always_inline, hot]]
inline static void write_separator(char sep, uint32_t count, char * write_pos)
{
	for (uint32_t i = 0; i < count; ++i)
	{
		*write_pos = sep;
		write_pos += 25;
	}
}

[[nodiscard]]
[[using gnu : always_inline]]
inline static int decompress(
	const char * begin,
	char sep,
	uint32_t line_count,
	char * output
)
{
	auto read_pos = from_simple_counter(begin, output);
	read_pos = from_differential_counter(read_pos, output + 2);

	write_separator(sep, line_count, output + 4);

	std::vector<uint16_t> buffer(line_count);

	read_pos = utils::fl2::decompress_vector(read_pos, buffer);
	if (!read_pos)
		return 1;
	from_vector(buffer, sep, output + 5);

	read_pos = utils::fl2::decompress_vector(read_pos, buffer);
	if (!read_pos)
		return 1;
	from_vector(buffer, sep, output + 10);

	read_pos = utils::fl2::decompress_vector(read_pos, buffer);
	if (!read_pos)
		return 1;
	from_vector(buffer, sep, output + 15);

	read_pos = utils::fl2::decompress_vector(read_pos, buffer);
	if (!read_pos)
		return 1;
	from_vector(buffer, '\n', output + 20);

	return 0;
}

}

#endif
