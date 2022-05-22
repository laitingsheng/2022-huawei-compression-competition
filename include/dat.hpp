#ifndef __DAT_HPP__
#define __DAT_HPP__

#include <cstdint>
#include <cstdio>

#include <array>
#include <concepts>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <fmt/core.h>

#include "utils.hpp"

namespace dat
{

template<uint8_t mantissa_width, std::signed_integral T>
[[nodiscard]]
[[using gnu : always_inline, hot, pure]]
inline static std::pair<T, uint8_t> float_to_integer(const char * pos)
{
	constexpr uint8_t max_digits = std::numeric_limits<T>::digits10;
	static_assert(max_digits > mantissa_width, "T cannot hold the floating point number");

	constexpr uint8_t max_integer_width = max_digits - mantissa_width;

	bool sign;
	uint8_t integer_width;
	T output;
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
		throw std::runtime_error("invalid leading character of the floating point number");

	for (c = *pos++; integer_width < max_integer_width && isdigit(c); c = *pos++, ++integer_width)
		output = output * 10 + (c - '0');
	if (c != '.')
		throw std::runtime_error("unprocessable integer part of the floating point");

	uint8_t parsed_mantissa_width = 0;
	for (c = *pos++; parsed_mantissa_width < mantissa_width && isdigit(c); c = *pos++, ++parsed_mantissa_width)
		output = output * 10 + (c - '0');

	if (sign)
		output |= std::numeric_limits<T>::min();

	return { output, integer_width + mantissa_width + sign + 1 };
}

[[using gnu : always_inline]]
inline static void compress(
	const char * begin,
	const char * end,
	char sep,
	char * output,
	size_t capacity,
	size_t & line_count,
	size_t & total
)
{
	utils::counter::differential<int64_t, size_t> counter_column0;
	std::array<std::vector<int64_t>, 70> standard_columns;

	line_count = 0;
	for (auto pos = begin; pos < end;)
	{
		auto [output, offset] = float_to_integer<3, int64_t>(pos);
		counter_column0.add(output);
		pos += offset;

		if (*pos++ != sep)
			throw std::runtime_error("invalid dat line");

		for (size_t i = 1; i < 71; ++i)
		{
			auto [output, offset] = float_to_integer<5, int64_t>(pos);
			standard_columns[i - 1].push_back(output);
			pos += offset;

			if (*pos++ != (i == 70 ? '\r' : sep))
				throw std::runtime_error("invalid dat line");
		}

		if (*pos++ != '\n')
			throw std::runtime_error("invalid dat line");

		++line_count;
	}
	counter_column0.commit();

	auto written = utils::counter::write(output, capacity, counter_column0.data());
	total = written;
	output += written;
	capacity -= written;

	utils::fl2::compressor compressor;

	for (const auto & column : standard_columns)
	{
		written = compressor(output, capacity, column);
		total += written;
		output += written;
		capacity -= written;
	}
}

template<uint8_t mantissa_width, std::signed_integral T>
[[nodiscard]]
[[using gnu : always_inline, hot]]
inline uint8_t write_integer_as_float(int64_t number, char * write_pos)
{
	constexpr T sign_mask = std::numeric_limits<T>::min();

	bool sign = number & sign_mask;
	if (sign)
	{
		*write_pos++ = '-';
		number &= ~sign_mask;
	}

	constexpr uint8_t max_length = std::numeric_limits<T>::digits10 + 1;

	std::array<char, max_length> buffer;

	uint8_t index = max_length;
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

	return max_length - index + sign;
}

[[using gnu : always_inline]]
inline static void decompress(const char * begin, char sep, size_t line_count, char * output)
{
	std::array<std::vector<int64_t>, 71> columns;
	for (auto & column : columns)
		column.resize(line_count);

	auto read = utils::counter::reconstruct_differential<int64_t, size_t>(begin, columns[0]);
	auto read_pos = begin + read;

	utils::fl2::decompressor decompressor;

	for (size_t i = 1; i < 71; ++i)
	{
		read = decompressor(read_pos, columns[i]);
		read_pos += read;
	}

	auto write_pos = output;
	for (size_t i = 0; i < line_count; ++i)
	{
		auto written = write_integer_as_float<3, int64_t>(columns[0][i], write_pos);
		write_pos += written;
		*write_pos++ = sep;

		for (size_t j = 1; j < 71; ++j)
		{
			written = write_integer_as_float<5, int64_t>(columns[j][i], write_pos);
			write_pos += written;
			*write_pos++ = j == 70 ? '\r' : sep;
		}

		*write_pos++ = '\n';
	}
}

}

#endif
