#ifndef __DAT_HPP__
#define __DAT_HPP__

#include <cstdint>
#include <cstdio>

#include <array>
#include <vector>

#include <fmt/core.h>

#include "utils.hpp"

namespace dat
{

inline static constexpr int64_t SIGN_MASK = uint64_t(1) << 61;

template<uint8_t mantissa_width>
[[nodiscard]]
[[using gnu : always_inline, hot]]
inline static const char * float_to_integer(const char * pos, char sep, int64_t & output)
{
	constexpr uint8_t max_integer_width = 18 - mantissa_width;

	bool sign;
	uint8_t integer_width;
	char c = *pos++;
	if (c == '-')
	{
		sign = true;
		integer_width = 0;
		output = 0;
	}
	else if (isdigit(c))
	{
		sign = false;
		integer_width = 1;
		output = c - '0';
	}
	else
	{
		fmt::print(stderr, "Invalid leading character of the floating point number\n");
		return nullptr;
	}

	for (c = *pos++; integer_width < max_integer_width && isdigit(c); c = *pos++, ++integer_width)
	{
		output *= 10;
		output += c - '0';
	}
	if (c != '.')
	{
		fmt::print(stderr, "Unprocessable integer part of the floating point.\n");
		return nullptr;
	}

	uint8_t parsed_mantissa_width = 0;
	for (c = *pos++; parsed_mantissa_width < mantissa_width && isdigit(c); c = *pos++, ++parsed_mantissa_width)
	{
		output *= 10;
		output += c - '0';
	}
	if (c != sep)
	{
		fmt::print(stderr, "Unprocessable mantissa part of the floating point.\n");
		return nullptr;
	}

	if (sign)
		output |= SIGN_MASK;

	return pos;
}

template<size_t N>
[[nodiscard]]
[[using gnu : always_inline, hot]]
inline static const char * readline(const char * pos, char sep, std::array<int64_t, N> & line)
{
	pos = float_to_integer<3>(pos, sep, line[0]);
	if (!pos)
		return nullptr;

	for (uint32_t i = 1; i < 70; ++i)
	{
		pos = float_to_integer<5>(pos, sep, line[i]);
		if (!pos)
			return nullptr;
	}

	pos = float_to_integer<5>(pos, '\r', line[70]);
	if (!pos)
		return nullptr;

	if (*pos++ != '\n')
	{
		fmt::print(stderr, "Invalid line ending\n");
		return nullptr;
	}

	return pos;
}

[[nodiscard]]
[[using gnu : always_inline]]
inline static int compress(
	const char * begin,
	const char * end,
	char sep,
	uint32_t & line_count,
	char * output,
	uint32_t capacity,
	uint32_t & written
)
{
	std::array<int64_t, 71> line;
	utils::counter::differential<int64_t, int64_t, uint32_t> counter0;
	utils::counter::simple<int64_t, uint32_t> counter59, counter70;
	std::array<std::vector<int64_t>, 68> columns;

	line_count = 0;
	for (auto pos = begin; pos < end;)
	{
		pos = readline(pos, sep, line);
		if (!pos)
			return 1;

		counter0.add(line[0]);
		for (uint32_t i = 0; i < 58; ++i)
			columns[i].push_back(line[i + 1]);
		counter59.add(line[59]);
		for (uint32_t i = 58; i < 68; ++i)
			columns[i].push_back(line[i + 2]);
		counter70.add(line[70]);

		++line_count;
	}
	counter0.commit();
	counter59.commit();
	counter70.commit();

	written = 0;

	auto write_pos = utils::counter::write(output, capacity, written, counter0.data());
	if (!write_pos)
		return 1;

	write_pos = utils::counter::write(write_pos, capacity, written, counter59.data());
	if (!write_pos)
		return 1;

	write_pos = utils::counter::write(write_pos, capacity, written, counter70.data());
	if (!write_pos)
		return 1;

	for (const auto & column : columns)
	{
		write_pos = utils::fl2::compress_vector(write_pos, capacity, written, column);
		if (!write_pos)
			return 1;
	}

	return 0;
}

[[nodiscard]]
[[using gnu : always_inline, hot]]
inline static const char * from_simple_counter(const char * read_pos, std::vector<int64_t> & output)
{
	auto size = *reinterpret_cast<const uint32_t *>(read_pos);
	read_pos += 4;

	for (uint32_t i = 0, line = 0; i < size; ++i)
	{
		auto value = *reinterpret_cast<const int64_t *>(read_pos);
		read_pos += 8;
		auto count = *reinterpret_cast<const uint32_t *>(read_pos);
		read_pos += 4;

		for (uint32_t j = 0; j < count; ++j)
			output[line++] = value;
	}

	return read_pos;
}

[[nodiscard]]
[[using gnu : always_inline, hot]]
inline static const char * from_differential_counter(const char * read_pos, std::vector<int64_t> & output)
{
	auto size = *reinterpret_cast<const uint32_t *>(read_pos);
	read_pos += 4;

	int64_t value = 0;
	for (uint32_t i = 0, line = 0; i < size; ++i)
	{
		auto diff = *reinterpret_cast<const int64_t *>(read_pos);
		read_pos += 8;
		auto count = *reinterpret_cast<const uint32_t *>(read_pos);
		read_pos += 4;

		for (uint32_t j = 0; j < count; ++j)
		{
			value += diff;
			output[line++] = value;
		}
	}

	return read_pos;
}

template<uint8_t mantissa_width>
[[using gnu : always_inline, hot]]
inline static char * write_integer_as_float(int64_t number, char * write_pos)
{
	if (number & SIGN_MASK)
	{
		*write_pos++ = '-';
		number &= ~SIGN_MASK;
	}

	std::array<char, 19> buffer;

	uint8_t index = 19;
	for (uint8_t i = 0; i < mantissa_width; ++i)
	{
		buffer[--index] = '0' + number % 10;
		number /= 10;
	}

	buffer[--index] = '.';

	if (number > 0)
	{
		while (number > 0)
		{
			buffer[--index] = '0' + number % 10;
			number /= 10;
		}
	}
	else
		buffer[--index] = '0';

	std::copy(buffer.begin() + index, buffer.end(), write_pos);

	return write_pos + 19 - index;
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
	std::array<std::vector<int64_t>, 71> columns;
	for (auto & column : columns)
		column.resize(line_count);

	auto read_pos = from_differential_counter(begin, columns[0]);
	if (!read_pos)
		return 1;

	read_pos = from_simple_counter(read_pos, columns[59]);
	if (!read_pos)
		return 1;

	read_pos = from_simple_counter(read_pos, columns[70]);
	if (!read_pos)
		return 1;

	for (uint32_t i = 1; i < 59; ++i)
	{
		read_pos = utils::fl2::decompress_vector(read_pos, columns[i]);
		if (!read_pos)
			return 1;
	}

	for (uint32_t i = 60; i < 70; ++i)
	{
		read_pos = utils::fl2::decompress_vector(read_pos, columns[i]);
		if (!read_pos)
			return 1;
	}

	auto write_pos = output;
	for (uint32_t i = 0; i < line_count; ++i)
	{
		write_pos = write_integer_as_float<3>(columns[0][i], write_pos);
		if (!write_pos)
			return 1;
		*write_pos++ = sep;

		for (uint32_t j = 1; j < 70; ++j)
		{
			write_pos = write_integer_as_float<5>(columns[j][i], write_pos);
			if (!write_pos)
				return 1;
			*write_pos++ = sep;
		}

		write_pos = write_integer_as_float<5>(columns[70][i], write_pos);
		if (!write_pos)
			return 1;
		*write_pos++ = '\r';
		*write_pos++ = '\n';
	}

	return 0;
}

}

#endif
