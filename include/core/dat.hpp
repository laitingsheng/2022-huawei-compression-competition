#ifndef __CORE_DAT_HPP__
#define __CORE_DAT_HPP__

#include <cctype>
#include <cstddef>
#include <cstdint>

#include <array>
#include <concepts>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/compile.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <mio/mmap.hpp>

#include "../utils/counter.hpp"
#include "../utils/fl2.hpp"
#include "../utils/utils.hpp"

namespace core::dat
{

template<std::signed_integral T, size_t mantissa_width>
[[nodiscard]]
[[using gnu : always_inline, hot, pure]]
inline static std::pair<T, size_t> float_to_integer(const char * pos)
{
	constexpr size_t max_digits = std::numeric_limits<T>::digits10;
	static_assert(max_digits > mantissa_width, "T cannot hold the floating point number");

	constexpr size_t max_integer_width = max_digits - mantissa_width;

	bool sign;
	size_t integer_width;
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
	[[unlikely]]
		throw std::runtime_error("invalid leading character of the floating point number");

	for (c = *pos++; integer_width < max_integer_width && isdigit(c); c = *pos++, ++integer_width)
	[[likely]]
		output = output * 10 + (c - '0');

	if (c != '.')
	[[unlikely]]
		throw std::runtime_error("unprocessable integer part of the floating point");

	size_t parsed_mantissa_width = 0;
	for (c = *pos++; parsed_mantissa_width < mantissa_width && isdigit(c); c = *pos++, ++parsed_mantissa_width)
	[[likely]]
		output = output * 10 + (c - '0');

	if (sign)
		output |= std::numeric_limits<T>::min();

	return { output, integer_width + mantissa_width + sign + 1 };
}

template<std::signed_integral T, size_t mantissa_width, char sep>
[[nodiscard]]
[[using gnu : always_inline, hot, pure]]
inline static std::tuple<T, size_t> parse_cell(const char * pos)
{
	auto [output, offset] = float_to_integer<T, mantissa_width>(pos);
	pos += offset;

	if (char c = *pos; c != sep)
	[[unlikely]]
		throw std::runtime_error(fmt::format(FMT_STRING("expect {:#02x}, got {:#02x}"), sep, c));

	return { output, offset + 1 };
}

template<std::unsigned_integral SizeT>
[[nodiscard]]
[[using gnu : always_inline, pure]]
inline static SizeT compress(const char * read_pos, size_t read_size, char * write_pos, size_t write_capacity)
{
	if (write_capacity < 1 + sizeof(SizeT))
	[[unlikely]]
		throw std::runtime_error("destination is too small");

	utils::counter::differential::differential<int64_t, SizeT> counter_column0;
	std::array<std::vector<int64_t>, 70> standard_columns;

	SizeT line_count = 0;
	size_t read = 0;
	while (read < read_size)
	[[likely]]
	{
		{
			auto [output, offset] = parse_cell<int64_t, 3, ' '>(read_pos);
			counter_column0.add(output);
			read_pos += offset;
			read += offset;
		}

		for (size_t i = 0; i < 69; ++i)
		[[likely]]
		{
			auto [output, offset] = parse_cell<int64_t, 5, ' '>(read_pos);
			standard_columns[i].push_back(output);
			read_pos += offset;
			read += offset;
		}

		{
			auto [output, offset] = parse_cell<int64_t, 5, '\r'>(read_pos);
			standard_columns[69].push_back(output);
			read_pos += offset;
			read += offset;
		}

		if (char c = *read_pos++; c != '\n')
		[[unlikely]]
			throw std::runtime_error(fmt::format(FMT_STRING("expect 0x0a, got {:#02x}"), c));
		++read;

		++line_count;
	}
	counter_column0.commit();

	if (read != read_size)
	[[unlikely]]
		throw std::runtime_error("unexpected end of file");

	*write_pos++ = static_cast<uint8_t>(utils::file_type::dat);
	auto numbers = reinterpret_cast<SizeT *>(write_pos);
	numbers[0] = read_size;
	numbers[1] = line_count;
	write_pos += 2 * sizeof(SizeT);
	SizeT size = 1 + 2 * sizeof(SizeT);
	write_capacity -= 1 + 2 * sizeof(SizeT);

	auto written = utils::counter::write(write_pos, write_capacity, counter_column0.data());
	write_pos += written;
	size += written;
	write_capacity -= written;

	utils::fl2::compressor compressor;

	for (const auto & column : standard_columns)
	{
		auto written = compressor.process<int64_t, SizeT>(write_pos, write_capacity, column);
		write_pos += written;
		size += written;
		write_capacity -= written;
	}

	return size;
}

template<std::signed_integral T, size_t mantissa_width>
[[nodiscard]]
[[using gnu : always_inline, hot]]
inline size_t write_integer_as_float(T number, char * write_pos)
{
	constexpr T sign_mask = std::numeric_limits<T>::min();

	bool sign = number & sign_mask;
	if (sign)
	{
		*write_pos++ = '-';
		number &= ~sign_mask;
	}

	constexpr size_t max_length = std::numeric_limits<T>::digits10 + 1;

	std::array<char, max_length> buffer;

	size_t index = max_length;
	for (size_t i = 0; i < mantissa_width; ++i)
	[[likely]]
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

template<std::unsigned_integral SizeT>
[[using gnu : always_inline]]
inline static void decompress(const char * read_pos, size_t read_size, const std::string & dest_path)
{
	auto numbers = reinterpret_cast<const SizeT *>(read_pos);
	const auto original_size = numbers[0], line_count = numbers[1];
	read_pos += 2 * sizeof(SizeT);
	read_size -= 2 * sizeof(SizeT);

	std::array<std::vector<int64_t>, 71> columns;
	for (auto & column : columns)
		column.resize(line_count);

	auto read = utils::counter::differential::reconstruct<int64_t, SizeT>(read_pos, read_size, columns[0]);
	read_pos += read;
	read_size -= read;

	utils::fl2::decompressor decompressor;

	for (size_t i = 1; i < 71; ++i)
	{
		read = decompressor.process<int64_t, SizeT>(read_pos, read_size, columns[i]);
		read_pos += read;
		read_size -= read;
	}

	if (read_size > 0)
		throw std::runtime_error("unexpected data at end of file");

	utils::blank_file(dest_path, original_size);
	auto dest = mio::mmap_sink(dest_path);

	auto write_pos = dest.data();
	for (size_t i = 0; i < line_count; ++i)
	[[likely]]
	{
		auto written = write_integer_as_float<int64_t, 3>(columns[0][i], write_pos);
		write_pos += written;
		*write_pos++ = ' ';

		for (size_t j = 1; j < 70; ++j)
		[[likely]]
		{
			written = dat::write_integer_as_float<int64_t, 5>(columns[j][i], write_pos);
			write_pos += written;
			*write_pos++ = ' ';
		}

		written = dat::write_integer_as_float<int64_t, 5>(columns[70][i], write_pos);
		write_pos += written;
		*write_pos++ = '\r';

		*write_pos++ = '\n';
	}
}

}

#endif
