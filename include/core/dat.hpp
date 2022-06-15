#ifndef __CORE_DAT_HPP__
#define __CORE_DAT_HPP__

#include <cctype>
#include <cstddef>
#include <cstdint>

#include <array>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <utility>

#include <fmt/compile.h>
#include <fmt/core.h>
#include <frozen/unordered_map.h>
#include <mio/mmap.hpp>

#include "../std.hpp"
#include "../utils/seq.hpp"
#include "../utils/traits.hpp"

namespace core::dat
{

class data final
{
	static constexpr auto hex_digits = std::array {
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
	};
	static constexpr frozen::unordered_map<char, uint8_t, 10> hex_digits_reverse {
		{ '0', 0 },
		{ '1', 1 },
		{ '2', 2 },
		{ '3', 3 },
		{ '4', 4 },
		{ '5', 5 },
		{ '6', 6 },
		{ '7', 7 },
		{ '8', 8 },
		{ '9', 9 }
	};

	std::array<std::vector<uint8_t>, 71> signs;
	std::array<std::vector<uint32_t>, 71> integers, mantissas;
	size_t line_count, file_size;

	explicit data() : signs(), integers(), mantissas(), line_count(0), file_size(0) {}

	template<char sep, size_t mantissa_width>
	[[nodiscard]]
	inline const char * parse_cell(const char * pos, size_t & size, size_t index)
	{
		constexpr size_t max_width = std::numeric_limits<uint32_t>::digits10;
		static_assert(mantissa_width <= max_width, "invalid mantissa_width");

		size_t width, total_width = 1;
		uint32_t number;
		char c = *pos++;
		if (c == '-')
		{
			signs[index].push_back(true);
			width = 0;
			number = 0;
		}
		else
		{
			signs[index].push_back(false);
			width = 1;
			number = hex_digits_reverse.at(c);
		}

		while (width < max_width + 1)
		{
			c = *pos++;
			++total_width;
			if (hex_digits_reverse.count(c))
			{
				++width;
				number = number * 10 + hex_digits_reverse.at(c);
			}
			else
			[[unlikely]]
				break;
		}

		if (c != '.')
		[[unlikely]]
			throw std::runtime_error("unexpected floating point format");
		integers[index].push_back(number);

		width = 0;
		number = 0;
		while (width < mantissa_width + 1)
		{
			c = *pos++;
			++total_width;
			if (hex_digits_reverse.count(c))
			{
				++width;
				number = number * 10 + hex_digits_reverse.at(c);
			}
			else
			[[unlikely]]
				break;
		}

		if (c != sep)
		[[unlikely]]
			throw std::runtime_error(fmt::format(FMT_COMPILE("expect separator {:#02x}, got {:#02x}"), sep, c));
		mantissas[index].push_back(number);

		size -= total_width;
		return pos;
	}

	template<char sep, size_t mantissa_width>
	[[nodiscard]]
	inline char * write_cell(char * pos, size_t & capacity, size_t line, size_t index) const
	{
		constexpr size_t max_width = std::numeric_limits<uint32_t>::digits10;
		static_assert(mantissa_width <= max_width, "invalid mantissa_width");
		constexpr size_t max_cell_width = 2 * max_width + 3;

		std::vector<char> buffer;
		buffer.reserve(max_cell_width);
		buffer.push_back(sep);
		auto number = mantissas[index][line];
		for (size_t i = 0; i < mantissa_width; ++i)
		{
			buffer.push_back(hex_digits.at(number % 10));
			number /= 10;
		}

		buffer.push_back('.');

		number = integers[index][line];
		if (number > 0)
		{
			while (number > 0)
			{
				buffer.push_back(hex_digits.at(number % 10));
				number /= 10;
			}
		}
		else
			buffer.push_back('0');

		if (signs[index][line])
			buffer.push_back('-');

		if (buffer.size() > capacity)
		[[unlikely]]
			throw std::runtime_error("insufficient capacity for the floating point number");

		std::copy(buffer.rbegin(), buffer.rend(), pos);

		capacity -= buffer.size();
		return pos + buffer.size();
	}
public:
	[[nodiscard]]
	inline static data decompress(utils::decompressor auto && decompressor, const char * pos, size_t size)
	{
		data re;

		auto constants = reinterpret_cast<const size_t *>(pos);
		auto line_count = re.line_count = constants[0];
		re.file_size = constants[1];

		for (auto & sign : re.signs)
			sign.resize(line_count);
		for (auto & integer : re.integers)
			integer.resize(line_count);
		for (auto & mantissa : re.mantissas)
			mantissa.resize(line_count);

		decompressor.start(reinterpret_cast<const std::byte *>(pos + 2 * sizeof(size_t)), size - 2 * sizeof(size_t));

		bool more = true;
		for (auto & sign : re.signs)
		{
			if (!more)
			[[unlikely]]
				throw std::runtime_error("insufficient data for decompression");
			more = decompressor(sign);
		}
		for (auto & integer : re.integers)
		{
			if (!more)
			[[unlikely]]
				throw std::runtime_error("insufficient data for decompression");
			more = decompressor(integer);
		}
		for (auto & mantissa : re.mantissas)
		{
			if (!more)
			[[unlikely]]
				throw std::runtime_error("insufficient data for decompression");
			more = decompressor(mantissa);
		}
		if (more)
		[[unlikely]]
			throw std::runtime_error("redundant data found at the end of the file");

		return re;
	}

	[[nodiscard]]
	inline static data parse(const char * pos, size_t size)
	{
		data re;

		re.file_size = size;
		size_t line_count = 0;
		while (size > 0)
		{
			pos = re.parse_cell<' ', 3>(pos, size, 0);
			for (size_t i = 1; i < 70; ++i)
				pos = re.parse_cell<' ', 5>(pos, size, i);
			pos = re.parse_cell<'\r', 5>(pos, size, 70);

			if (*pos++ != '\n')
			[[unlikely]]
				throw std::runtime_error("invalid line separator");
			--size;

			++line_count;
		}

		re.line_count = line_count;
		for (auto & sign : re.signs)
			sign.shrink_to_fit();
		for (auto & integer : re.integers)
			integer.shrink_to_fit();
		for (auto & mantissa : re.mantissas)
			mantissa.shrink_to_fit();

		return re;
	}

	~data() noexcept = default;

	data(const data &) = default;
	data(data &&) noexcept = default;

	data & operator=(const data &) = default;
	data & operator=(data &&) noexcept = default;

	inline void compress(utils::compressor auto && compressor, std::path_like auto && path) const
	{
		static constexpr size_t leading_size = 1 + 2 * sizeof(size_t);

		size_t capacity = leading_size + line_count * 71 * sizeof(uint64_t);
		utils::blank_file(path, capacity);
		auto file = mio::mmap_sink(path);
		auto pos = file.data();

		*pos = static_cast<uint8_t>(utils::file_type::dat);
		auto constants = reinterpret_cast<size_t *>(pos + 1);
		constants[0] = line_count;
		constants[1] = file_size;

		compressor.start(reinterpret_cast<std::byte *>(pos + leading_size), capacity - leading_size);

		for (const auto & sign : signs)
			compressor(sign);
		for (const auto & integer : integers)
			compressor(integer);
		for (const auto & mantissa : mantissas)
			compressor(mantissa);

		std::filesystem::resize_file(path, leading_size + compressor.stop());
	}

	inline void write(std::path_like auto && path) const
	{
		utils::blank_file(path, file_size);
		auto file = mio::mmap_sink(path);
		auto pos = file.data();

		auto capacity = file_size;
		for (size_t line = 0; line < line_count; ++line)
		{
			pos = write_cell<' ', 3>(pos, capacity, line, 0);
			for (size_t i = 1; i < 70; ++i)
				pos = write_cell<' ', 5>(pos, capacity, line, i);
			pos = write_cell<'\r', 5>(pos, capacity, line, 70);

			*pos++ = '\n';
			--capacity;
		}

		if (capacity > 0)
			throw std::runtime_error("unexpected uncompressed size");
	}
};

}

#endif
