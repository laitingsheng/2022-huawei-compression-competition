#ifndef __HXV_HPP__
#define __HXV_HPP__

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <concepts>
#include <stdexcept>
#include <vector>

#include "utils.hpp"

namespace hxv
{

[[nodiscard]]
[[using gnu : always_inline, hot, const]]
inline static uint8_t from_hex_char(char hex)
{
	if (hex >= '0' && hex <= '9')
		return hex - '0';
	if (hex >= 'A' && hex <= 'F')
		return hex - 'A' + 10;
	throw std::invalid_argument("invalid hex character");
}

[[nodiscard]]
[[using gnu : always_inline, hot, pure]]
inline static uint8_t from_hex_chars(const char * input)
{
	return from_hex_char(input[0]) << 4 | from_hex_char(input[1]);
}

[[using gnu : always_inline]]
inline void compress(
	const char * begin,
	const char * end,
	char sep,
	char * output,
	size_t capacity,
	size_t & line_count,
	size_t & total
)
{
	line_count = 0;

	utils::counter::differential<uint8_t, size_t> counter_column1;
	std::array<std::vector<uint8_t>, 9> standard_columns;

	for (auto pos = begin; pos < end;)
	{
		standard_columns[0].push_back(from_hex_chars(pos));
		pos += 2;

		counter_column1.add(from_hex_chars(pos));
		pos += 2;

		if (*pos++ != sep)
			throw std::invalid_argument("invalid hxv line");

		for (size_t i = 2; i < 10; i += 2)
		{
			standard_columns[i - 1].push_back(from_hex_chars(pos));
			pos += 2;

			standard_columns[i].push_back(from_hex_chars(pos));
			pos += 2;

			if (*pos++ != (i == 8 ? '\n' : sep))
				throw std::invalid_argument("invalid hxv line");
		}

		++line_count;
	}
	counter_column1.commit();

	auto written = utils::counter::write(output, capacity, counter_column1.data());
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

[[nodiscard]]
[[using gnu : always_inline, hot, const]]
inline static char to_hex_char(uint8_t value)
{
	if (value >= 16)
		throw std::invalid_argument("invalid hex value");
	return value < 10 ? '0' + value : 'A' + value - 10;
}

[[using gnu : always_inline, hot]]
inline static void to_hex_chars(uint8_t value, char * output)
{
	output[0] = to_hex_char(value >> 4);
	output[1] = to_hex_char(value & 0xF);
}

[[using gnu : always_inline, hot]]
inline static void write_vector(const std::vector<uint8_t> & data, char * write_pos)
{
	for (size_t i = 0; i < data.size(); ++i)
	{
		to_hex_chars(data[i], write_pos);
		write_pos += 25;
	}
}

[[using gnu : always_inline, hot]]
inline static void write_separator(char sep, size_t count, char * write_pos)
{
	for (size_t i = 0; i < count; ++i)
	{
		*write_pos = sep;
		write_pos += 25;
	}
}

[[using gnu : always_inline]]
inline static void decompress(const char * begin, char sep, size_t line_count, char * output)
{
	std::vector<uint8_t> buffer(line_count);

	auto read = utils::counter::reconstruct_differential<uint8_t, size_t>(begin, buffer);
	auto read_pos = begin + read;
	write_vector(buffer, output + 2);

	utils::fl2::decompressor decompressor;

	read = decompressor(read_pos, buffer);
	read_pos += read;
	write_vector(buffer, output);

	write_separator(sep, line_count, output + 4);

	for (size_t offset = 5; offset < 25; offset += 5)
	{
		read = decompressor(read_pos, buffer);
		read_pos += read;
		write_vector(buffer, output + offset);

		read = decompressor(read_pos, buffer);
		read_pos += read;
		write_vector(buffer, output + offset + 2);

		write_separator(offset == 20 ? '\n' : sep, line_count, output + offset + 4);
	}
}

}

#endif
