#ifndef __CORE_HXV_HPP__
#define __CORE_HXV_HPP__

#include <cstddef>
#include <cstdint>

#include <array>
#include <concepts>
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

namespace core::hxv
{

[[nodiscard]]
[[using gnu : always_inline, hot, const]]
inline static uint8_t from_hex_char(char hex)
{
	if (hex >= '0' && hex <= '9')
		return hex - '0';
	else if (hex >= 'A' && hex <= 'F')
		return hex - 'A' + 10;
	else
	[[unlikely]]
		throw std::invalid_argument(fmt::format(FMT_STRING("invalid hex character ASCII {:#02x}"), hex));
}

[[nodiscard]]
[[using gnu : always_inline, hot, pure]]
inline static uint8_t from_hex_chars(const char * input)
{
	return from_hex_char(input[0]) << 4 | from_hex_char(input[1]);
}

template<char sep>
[[nodiscard]]
[[using gnu : always_inline, hot, pure]]
inline static std::tuple<uint8_t, uint8_t, size_t> parse_cell(const char * pos)
{
	uint8_t first = from_hex_chars(pos);
	pos += 2;

	uint8_t second = from_hex_chars(pos);
	pos += 2;

	if (char c = *pos; c != sep)
	[[unlikely]]
		throw std::runtime_error(fmt::format(FMT_STRING("expect {:#02x}, got {:#02x}"), sep, c));

	return { first, second, 5 };
}

template<std::unsigned_integral SizeT>
[[nodiscard]]
[[using gnu : always_inline]]
inline static size_t compress(const char * read_pos, size_t read_size, char * write_pos, size_t write_capacity)
{
	if (write_capacity < 1 + sizeof(SizeT))
	[[unlikely]]
		throw std::runtime_error("destination is too small");

	utils::counter::differential::differential<uint8_t, SizeT> counter_column1;
	std::array<std::vector<uint8_t>, 9> standard_columns;

	SizeT line_count = 0;
	size_t read = 0;
	while (read < read_size)
	[[likely]]
	{
		{
			auto [first, second, offset] = parse_cell<','>(read_pos);
			standard_columns[0].push_back(first);
			counter_column1.add(second);
			read_pos += offset;
			read += offset;
		}

		for (size_t i = 1; i < 7; i += 2)
		[[likely]]
		{
			auto [first, second, offset] = parse_cell<','>(read_pos);
			standard_columns[i].push_back(first);
			standard_columns[i + 1].push_back(second);
			read_pos += offset;
			read += offset;
		}

		{
			auto [first, second, offset] = parse_cell<'\n'>(read_pos);
			standard_columns[7].push_back(first);
			standard_columns[8].push_back(second);
			read_pos += offset;
			read += offset;
		}

		++line_count;
	}
	counter_column1.commit();

	if (read != read_size)
	[[unlikely]]
		throw std::runtime_error("unexpected end of file");

	*write_pos++ = static_cast<uint8_t>(utils::file_type::hxv);
	*reinterpret_cast<SizeT *>(write_pos) = line_count;
	write_pos += sizeof(SizeT);
	SizeT size = 1 + sizeof(SizeT);
	write_capacity -= 1 + sizeof(SizeT);

	auto written = utils::counter::write(write_pos, write_capacity, counter_column1.data());
	write_pos += written;
	size += written;
	write_capacity -= written;

	utils::fl2::compressor compressor;

	for (const auto & column : standard_columns)
	{
		auto written = compressor.process<uint8_t, SizeT>(write_pos, write_capacity, column);
		write_pos += written;
		size += written;
		write_capacity -= written;
	}

	return size;
}

[[nodiscard]]
[[using gnu : always_inline, hot, const]]
inline static char to_hex_char(uint8_t value)
{
	if (value >= 16)
	[[unlikely]]
		throw std::invalid_argument(fmt::format(FMT_STRING("value should not exceed 0xf, got 0x{:x}"), value));
	else
	[[likely]]
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
	[[likely]]
	{
		to_hex_chars(data[i], write_pos);
		write_pos += 25;
	}
}

template<char sep>
[[using gnu : always_inline, hot]]
inline static void write_separator(size_t count, char * write_pos)
{
	for (size_t i = 0; i < count; ++i)
	[[likely]]
	{
		*write_pos = sep;
		write_pos += 25;
	}
}

template<std::unsigned_integral SizeT>
[[using gnu : always_inline]]
inline static void decompress(const char * read_pos, size_t read_size, const std::string & dest_path)
{
	const auto line_count = *reinterpret_cast<const SizeT *>(read_pos);
	read_pos += sizeof(SizeT);
	read_size -= sizeof(SizeT);

	utils::blank_file(dest_path, line_count * 25);
	auto dest = mio::mmap_sink(dest_path);

	std::vector<uint8_t> buffer(line_count);

	auto read = utils::counter::differential::reconstruct<uint8_t, SizeT>(read_pos, read_size, buffer);
	read_pos += read;
	read_size -= read;
	hxv::write_vector(buffer, dest.data() + 2);

	utils::fl2::decompressor decompressor;

	read = decompressor.process<uint8_t, SizeT>(read_pos, read_size, buffer);
	read_pos += read;
	read_size -= read;
	hxv::write_vector(buffer, dest.data());

	write_separator<','>(line_count, dest.data() + 4);

	for (size_t offset = 5; offset < 20; offset += 5)
	[[likely]]
	{
		read = decompressor.process<uint8_t, SizeT>(read_pos, read_size, buffer);
		read_pos += read;
		read_size -= read;
		write_vector(buffer, dest.data() + offset);

		read = decompressor.process<uint8_t, SizeT>(read_pos, read_size, buffer);
		read_pos += read;
		read_size -= read;
		write_vector(buffer, dest.data() + offset + 2);

		write_separator<','>(line_count, dest.data() + offset + 4);
	}

	read = decompressor.process<uint8_t, SizeT>(read_pos, read_size, buffer);
	read_pos += read;
	read_size -= read;
	write_vector(buffer, dest.data() + 20);

	read = decompressor.process<uint8_t, SizeT>(read_pos, read_size, buffer);
	read_pos += read;
	read_size -= read;
	write_vector(buffer, dest.data() + 22);

	write_separator<'\n'>(line_count, dest.data() + 24);

	if (read_size > 0)
		throw std::runtime_error("unexpected data at end of file");
}

}

#endif
