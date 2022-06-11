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
	static constexpr size_t line_width = 25;

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

	template<std::unsigned_integral T, size_t width, bool sign, char sep, size_t index>
	[[nodiscard]]
	inline static auto advance(const char * pos)
	{
		static_assert(width > 0 && width <= sizeof(T) * 2, "invalid width for the specified type");
		static_assert(index <= width, "index should always be less than the width");

		T value = 0;
		for (size_t i = 0; i <= width; ++i)
		{
			if (i == index)
			{
				if (pos[i] != sep)
				[[unlikely]]
					throw std::runtime_error("invalid HXV format (separator)");
			}
			else
			{
				value <<= 4;
				value |= from_hex_char(pos[i]);
			}
		}

		if constexpr(sign)
		{
			static constexpr T mask = 1 << ((width << 2) - 1);
			return std::pair<bool, T>(value & mask, value & (mask - 1));
		}
		else
			return value;
	}

	template<std::unsigned_integral T, size_t width, bool sign>
	[[nodiscard]]
	inline static auto advance(const char * pos)
	{
		static_assert(width > 0 && width <= sizeof(T) * 2, "invalid width");

		T value = 0;
		for (size_t i = 0; i < width; ++i)
		{
			value <<= 4;
			value |= from_hex_char(pos[i]);
		}

		if constexpr(sign)
		{
			static constexpr T mask = 1 << ((width << 2) - 1);
			return std::pair<bool, T>(value & mask, value & (mask - 1));
		}
		else
			return value;
	}

	template<std::unsigned_integral T, size_t width, char sep, size_t index>
	inline static void write(char * pos, T value, bool sign = false)
	{
		static_assert(width > 0 && width <= sizeof(T) * 2, "invalid width for the specified type");
		static_assert(index <= width, "index should always be less than the width");

		if (sign)
		{
			static constexpr T mask = 1 << ((width << 2) - 1);
			value |= mask;
		}

		for (size_t i = width + 1; i > 0; --i)
		{
			if (i == index + 1)
				pos[i - 1] = sep;
			else
			{
				pos[i - 1] = to_hex_char(value & 0xF);
				value >>= 4;
			}
		}
	}

	template<std::unsigned_integral T, size_t width>
	inline static void write(char * pos, T value, bool sign = false)
	{
		static_assert(width > 0 && width <= sizeof(T) * 2, "invalid width for the specified type");

		if (sign)
		{
			static constexpr T mask = 1 << ((width << 2) - 1);
			value |= mask;
		}

		for (size_t i = width; i > 0; --i)
		{
			pos[i - 1] = to_hex_char(value & 0xF);
			value >>= 4;
		}
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

	std::vector<uint8_t> ss, nn;
	std::array<std::vector<uint8_t>, 3> signs;
	std::array<std::vector<uint16_t>, 5> columns;
	size_t line_count;

	explicit data() : columns(), line_count(0) {}
public:
	[[nodiscard]]
	inline static data decompress(utils::decompressor auto && decompressor, const char * pos, size_t size)
	{
		data re;

		const auto line_count = re.line_count = *reinterpret_cast<const size_t *>(pos);
		pos += sizeof(size_t);
		size -= sizeof(size_t);

		re.ss.resize(line_count);
		re.nn.resize(line_count);
		for (auto & sign : re.signs)
			sign.resize(line_count);
		for (auto & column : re.columns)
			column.resize(line_count);

		auto read = decompressor(pos, size, re.ss);
		pos += read;
		size -= read;

		read = decompressor(pos, size, re.nn);
		pos += read;
		size -= read;
		utils::seq::diff::reconstruct(re.nn);

		for (auto & sign : re.signs)
		{
			read = decompressor(pos, size, sign);
			pos += read;
			size -= read;
		}

		for (auto & column : re.columns)
		{
			read = decompressor(pos, size, column);
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
		while (size >= line_width)
		{
			re.ss.push_back(advance<uint8_t, 2, false>(pos));
			pos += 2;
			re.nn.push_back(advance<uint8_t, 2, false, ',', 2>(pos));
			pos += 3;

			re.columns[0].push_back(advance<uint16_t, 4, false, ',', 4>(pos));
			pos += 5;

			{
				auto [sign, value] = advance<uint16_t, 3, true>(pos);
				re.signs[0].push_back(sign);
				re.columns[1].push_back(value);
				pos += 3;
			}

			{
				auto [sign, value] = advance<uint16_t, 3, true, ',', 1>(pos);
				re.signs[1].push_back(sign);
				re.columns[2].push_back(value);
				pos += 4;
			}

			{
				auto [sign, value] = advance<uint16_t, 3, true, ',', 2>(pos);
				re.signs[2].push_back(sign);
				re.columns[3].push_back(value);
				pos += 4;
			}

			re.columns[4].push_back(advance<uint16_t, 3, false, '\n', 3>(pos));
			pos += 4;

			size -= line_width;
			++line_count;
		}

		if (size)
		[[unlikely]]
			throw std::runtime_error("invalid HXV format");

		re.ss.shrink_to_fit();
		re.nn.shrink_to_fit();
		for (auto & sign : re.signs)
			sign.shrink_to_fit();
		for (auto & column : re.columns)
			column.shrink_to_fit();
		re.line_count = line_count;

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

		auto written = compressor(pos, capacity, ss);
		total += written;
		pos += written;
		capacity -= written;

		written = compressor(pos, capacity, utils::seq::diff::construct(nn));
		total += written;
		pos += written;
		capacity -= written;

		for (const auto & sign : signs)
		{
			written = compressor(pos, capacity, sign);
			total += written;
			pos += written;
			capacity -= written;
		}

		for (const auto & column : columns)
		{
			written = compressor(pos, capacity, column);
			total += written;
			pos += written;
			capacity -= written;
		}

		std::filesystem::resize_file(path, total);
	}

	inline void write(std::path_like auto && path) const
	{
		utils::blank_file(path, line_count * line_width);
		auto file = mio::mmap_sink(path);
		auto pos = file.data();

		for (size_t line = 0; line < line_count; ++line)
		{
			write<uint8_t, 2>(pos, ss[line]);
			pos += 2;

			write<uint8_t, 2, ',', 2>(pos, nn[line]);
			pos += 3;

			write<uint16_t, 4, ',', 4>(pos, columns[0][line]);
			pos += 5;

			write<uint16_t, 3>(pos, columns[1][line], signs[0][line]);
			pos += 3;

			write<uint16_t, 3, ',', 1>(pos, columns[2][line], signs[1][line]);
			pos += 4;

			write<uint16_t, 3, ',', 2>(pos, columns[3][line], signs[2][line]);
			pos += 4;

			write<uint16_t, 3, '\n', 3>(pos, columns[4][line]);
			pos += 4;
		}
	}
};

}

#endif
