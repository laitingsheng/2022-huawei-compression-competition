#ifndef __CORE_HXV_HPP__
#define __CORE_HXV_HPP__

#include <cstddef>
#include <cstdint>

#include <array>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include <fmt/compile.h>
#include <fmt/core.h>
#include <mio/mmap.hpp>

#include "../std.hpp"
#include "../utils/seq.hpp"
#include "../utils/traits.hpp"

namespace core::hxv
{

class data final
{
	[[nodiscard]]
	inline static constexpr uint8_t from_hex_char(char hex)
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
	inline static uint8_t from_hex_chars(const char * pos)
	{
		return from_hex_char(pos[0]) << 4 | from_hex_char(pos[1]);
	}

	[[nodiscard]]
	inline static constexpr char to_hex_char(uint8_t value)
	{
		if (value < 16)
			return value < 10 ? '0' + value : 'A' + value - 10;
		else
		[[unlikely]]
			throw std::invalid_argument(fmt::format(FMT_STRING("value should not exceed 0xf, got 0x{:x}"), value));
	}

	inline static void to_hex_chars(uint8_t value, char * output)
	{
		output[0] = to_hex_char(value >> 4);
		output[1] = to_hex_char(value & 0xF);
	}

	std::array<std::vector<uint8_t>, 10> columns;
	size_t line_count;

	explicit data() : columns(), line_count(0) {}

	template<char sep>
	[[nodiscard]]
	inline const char * parse_cell(const char * pos, size_t & size, size_t index)
	{
		if (size < 5)
			throw std::runtime_error("insufficient data for parsing");

		columns[index].push_back(from_hex_chars(pos));
		pos += 2;

		columns[index + 1].push_back(from_hex_chars(pos));
		pos += 2;

		if (char c = *pos++; c != sep)
		[[unlikely]]
			throw std::runtime_error(fmt::format(FMT_STRING("expect {:#02x}, got {:#02x}"), sep, c));

		size -= 5;
		return pos;
	}

	template<char sep>
	[[nodiscard]]
	inline char * write_cell(char * pos, size_t & capacity, size_t line, size_t index) const
	{
		if (capacity < 5)
			throw std::runtime_error("insufficient capacity for output");

		to_hex_chars(columns[index][line], pos);
		pos += 2;

		to_hex_chars(columns[index + 1][line], pos);
		pos += 2;

		*pos++ = sep;

		capacity -= 5;
		return pos;
	}
public:
	[[nodiscard]]
	inline static data decompress(utils::decompressor auto && decompressor, const char * pos, size_t size)
	{
		data re;

		const auto line_count = re.line_count = *reinterpret_cast<const size_t *>(pos);
		pos += sizeof(size_t);
		size -= sizeof(size_t);

		for (auto & column : re.columns)
			column.resize(line_count);

		auto read = decompressor(pos, size, re.columns[0]);
		pos += read;
		size -= read;

		read = decompressor(pos, size, re.columns[1]);
		pos += read;
		size -= read;
		utils::seq::diff::reconstruct(re.columns[1]);

		for (size_t i = 2; i < 10; ++i)
		{
			read = decompressor(pos, size, re.columns[i]);
			pos += read;
			size -= read;
		}

		if (size > 0)
			throw std::runtime_error("redundant data found in the compressed file");

		return re;
	}

	[[nodiscard]]
	inline static data parse(const char * pos, size_t size)
	{
		data re;

		size_t line_count = 0;
		while (size > 0)
		{
			for (size_t i = 0; i < 8; i += 2)
				pos = re.parse_cell<','>(pos, size, i);
			pos = re.parse_cell<'\n'>(pos, size, 8);

			++line_count;
		}

		re.line_count = line_count;
		for (auto & column : re.columns)
			column.shrink_to_fit();

		return re;
	}

	~data() noexcept = default;

	data(const data &) = default;
	data(data &&) noexcept = default;

	data & operator=(const data &) = default;
	data & operator=(data &&) noexcept = default;

	inline void compress(utils::compressor auto && compressor, std::path_like auto && path) const
	{
		static constexpr size_t leading_size = 1 + sizeof(size_t);

		size_t capacity = leading_size + 10 * line_count * sizeof(uint8_t);
		utils::blank_file(path, capacity);
		auto file = mio::mmap_sink(path);
		auto pos = file.data();

		*pos++ = static_cast<uint8_t>(utils::file_type::hxv);
		*reinterpret_cast<size_t *>(pos) = line_count;
		pos += sizeof(size_t);
		capacity -= leading_size;
		size_t total = leading_size;

		auto written = compressor(pos, capacity, columns[0]);
		total += written;
		pos += written;
		capacity -= written;

		written = compressor(pos, capacity, utils::seq::diff::construct(columns[1]));
		total += written;
		pos += written;
		capacity -= written;

		for (size_t i = 2; i < 10; ++i)
		{
			written = compressor(pos, capacity, columns[i]);
			total += written;
			pos += written;
			capacity -= written;
		}

		std::filesystem::resize_file(path, total);
	}

	inline void write(std::path_like auto && path) const
	{
		size_t capacity = line_count * 25;
		utils::blank_file(path, capacity);
		auto file = mio::mmap_sink(path);
		auto pos = file.data();

		for (size_t line = 0; line < line_count; ++line)
		{
			for (size_t i = 0; i < 8; i += 2)
				pos = write_cell<','>(pos, capacity, line, i);
			pos = write_cell<'\n'>(pos, capacity, line, 8);
		}

		if (capacity > 0)
			throw std::runtime_error("unexpected uncompressed size");
	}
};

}

#endif
