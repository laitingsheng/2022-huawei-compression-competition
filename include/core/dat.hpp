#ifndef __CORE_DAT_HPP__
#define __CORE_DAT_HPP__

#include <cstddef>
#include <cstdint>

#include <algorithm>
#include <array>
#include <concepts>
#include <filesystem>
#include <limits>
#include <list>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/compile.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <frozen/unordered_map.h>
#include <mio/mmap.hpp>

#include "../utils/seq.hpp"
#include "../utils/traits.hpp"

namespace core::dat
{

template<std::unsigned_integral T, size_t N>
class chunk final
{
	static constexpr auto digits = std::array {
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
	};

	[[noreturn]]
	inline static constexpr void insufficient()
	{
		throw std::runtime_error("insufficient output buffer");
	}

	template<size_t mantissa_width>
	[[nodiscard]]
	inline static size_t write_cell(char * pos, size_t capacity, bool sign, T value)
	{
		static constexpr size_t max_width = std::numeric_limits<T>::digits10;
		static_assert(mantissa_width < max_width, "invalid mantissa_width");

		std::vector<char> buffer;
		buffer.reserve(max_width + 3);

		for (size_t i = 0; i < mantissa_width; ++i)
		{
			buffer.push_back(digits.at(value % 10));
			value /= 10;
		}

		buffer.push_back('.');

		if (value > 0)
		{
			while (value > 0)
			{
				buffer.push_back(digits.at(value % 10));
				value /= 10;
			}
		}
		else
			buffer.push_back('0');

		if (sign)
			buffer.push_back('-');

		if (buffer.size() > capacity)
		[[unlikely]]
			insufficient();

		std::ranges::reverse_copy(buffer, pos);
		return buffer.size();
	}

	uint64_t time_start, time_stop, time_step, lines;
	std::array<T, N> mins;
	std::array<std::vector<uint8_t>, N> signs;
	std::array<std::vector<T>, N> columns;
public:
	[[nodiscard]]
	inline static chunk decompress(utils::decompressor auto & decompressor)
	{
		chunk re;

		decompressor(re.time_start);
		decompressor(re.time_stop);
		decompressor(re.time_step);
		decompressor(re.lines);
		auto lines = re.lines;

		decompressor(re.mins);

		for (auto & sign : re.signs)
		{
			sign.resize(lines);
			decompressor(sign);
		}

		for (size_t i = 0; i < N; ++i)
		{
			T min = re.mins[i];
			auto & column = re.columns[i];

			column.resize(lines);
			decompressor(column);
			for (T & value : column)
				value += min;
		}

		return re;
	}

	explicit chunk() : lines(0), mins(), signs(), columns() {}

	chunk(const chunk &) = delete;
	chunk(chunk &&) = default;

	~chunk() noexcept = default;

	chunk & operator=(const chunk &) = delete;
	chunk & operator=(chunk &&) = default;

	[[nodiscard]]
	inline bool accept(uint64_t time, const std::array<bool, N> & signs, const std::array<T, N> & values)
	{
		if (lines < 1)
		{
			time_start = time_stop = time;
			mins = values;
		}
		else
		{
			if (time < time_start)
				return false;

			if (lines < 2)
			{

				time_stop = time;
				time_step = time - time_start;
			}
			else
			{
				if (time - time_stop != time_step)
					return false;
				time_stop = time;
			}

			for (size_t i = 0; i < N; ++i)
				if (mins[i] > values[i])
					mins[i] = values[i];
		}

		for (size_t i = 0; i < N; ++i)
		{
			this->signs[i].push_back(signs[i]);
			columns[i].push_back(values[i]);
		}
		++lines;
		return true;
	}

	inline void compress(utils::compressor auto & compressor) const
	{
		compressor(time_start);
		compressor(time_stop);
		compressor(time_step);
		compressor(lines);

		compressor(mins);

		for (const auto & sign : signs)
			compressor(sign);

		for (size_t i = 0; i < N; ++i)
		{
			T min = mins[i];
			auto buffer = columns[i];
			for (T & value : buffer)
				value -= min;
			compressor(buffer);
		}
	}

	inline size_t write(char * pos, size_t capacity) const
	{
		size_t written = 0;

		size_t time = time_start;
		for (size_t l = 0; l < lines; ++l)
		{
			auto written = write_cell<3>(pos, capacity, false, time);
			pos += written;
			capacity -= written;

			for (size_t i = 0; i < N; ++i)
			{
				if (capacity < 1)
				[[unlikely]]
					insufficient();
				*pos++ = ' ';
				--capacity;

				written = write_cell<5>(pos, capacity, signs[i][l], columns[i][l]);
				pos += written;
				capacity -= written;
			}

			if (capacity < 2)
			[[unlikely]]
				insufficient();
			*pos++ = '\r';
			*pos++ = '\n';

			time += time_step;
		}

		if (time != time_stop)
		[[unlikely]]
			throw std::runtime_error("corrupted chunk");

		return written;
	}
};

class data final
{
	static constexpr frozen::unordered_map<char, uint8_t, 10> digits_reverse {
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

	std::list<chunk<uint64_t, 70>> chunks;
	size_t file_size;

	explicit data() : chunks(), file_size(0) {}

	template<char exp>
	inline static constexpr void match(char c)
	{
		if (c != exp)
		[[unlikely]]
			throw std::runtime_error(fmt::format(FMT_COMPILE("expect {:#02x}, got {:#02x}"), exp, c));
	}

	template<size_t mantissa_width>
	[[nodiscard]]
	inline static std::tuple<bool, uint64_t, size_t> parse_cell(const char * pos, size_t size)
	{
		static constexpr size_t max_integer_width = std::numeric_limits<uint64_t>::digits10 - mantissa_width;

		bool sign;
		size_t width, total_width = 1;
		uint64_t number;
		char c = *pos++;
		if (c == '-')
		{
			sign = true;
			width = 0;
			number = 0;
		}
		else
		{
			sign = false;
			width = 1;
			if (!digits_reverse.count(c))
			[[unlikely]]
				throw std::runtime_error(fmt::format(FMT_COMPILE("unknown character {:#02x}"), c));
			number = digits_reverse.at(c);
		}

		for (
			c = *pos++, ++total_width;
			total_width < size && width < max_integer_width && digits_reverse.count(c);
			number = number * 10 + digits_reverse.at(c), ++width, c = *pos++, ++total_width
		);

		match<'.'>(c);

		for (
			width = 0, c = *pos++;
			total_width < size && width < mantissa_width && digits_reverse.count(c);
			number = number * 10 + digits_reverse.at(c), ++width, c = *pos++, ++total_width
		);

		if (width < mantissa_width)
		[[unlikely]]
			throw std::runtime_error(fmt::format(FMT_COMPILE("expect {} mantissas, got {}"), mantissa_width, width));

		return { sign, number, total_width };
	}
public:
	[[nodiscard]]
	inline static data decompress(utils::decompressor auto & decompressor, const char * pos, size_t size)
	{
		data re;

		decompressor.start(reinterpret_cast<const std::byte *>(pos), size);

		decompressor(re.file_size);

		while (decompressor.is_streaming())
			re.chunks.emplace_back(chunk<uint64_t, 70>::decompress(decompressor));

		decompressor.stop();

		return re;
	}

	[[nodiscard]]
	inline static data parse(const char * pos, size_t size)
	{
		data re;

		re.file_size = size;

		std::array<bool, 70> signs;
		std::array<uint64_t, 70> values;
		size_t line = 0;
		while (size > 0)
		{
			auto [sign, time, width] = parse_cell<3>(pos, size);
			if (sign)
			[[unlikely]]
				throw std::runtime_error("negative time is not supported");
			pos += width;
			size -= width;

			for (size_t i = 0; i < 70; ++i)
			{
				if (size < 1)
				[[unlikely]]
					throw std::runtime_error(fmt::format(FMT_COMPILE("incomplete line @ {}"), line));
				match<' '>(*pos++);
				--size;

				auto [sign, value, width] = parse_cell<5>(pos, size);
				signs[i] = sign;
				values[i] = value;
				pos += width;
				size -= width;
			}

			if (size < 2)
			[[unlikely]]
				throw std::runtime_error(fmt::format(FMT_COMPILE("incomplete line @ {}"), line));
			match<'\r'>(*pos++);
			match<'\n'>(*pos++);
			size -= 2;

			if (re.chunks.size())
			{
				if (!re.chunks.back().accept(time, signs, values))
					if (!re.chunks.emplace_back().accept(time, signs, values))
					[[unlikely]]
						throw std::runtime_error("fails to create a new chunk");
			}
			else
			{
				if (!re.chunks.emplace_back().accept(time, signs, values))
				[[unlikely]]
					throw std::runtime_error("failed to initialise the chunks");
			}

			++line;
		}

		return re;
	}

	~data() noexcept = default;

	data(const data &) = default;
	data(data &&) noexcept = default;

	data & operator=(const data &) = default;
	data & operator=(data &&) noexcept = default;

	inline void compress(utils::compressor auto & compressor, const std::path_like auto & path) const
	{
		size_t capacity = file_size;
		utils::blank_file(path, capacity);
		auto file = mio::mmap_sink(path);
		auto pos = file.data();

		*pos = static_cast<uint8_t>(utils::file_type::dat);

		compressor.start(reinterpret_cast<std::byte *>(pos + 1), capacity - 1);

		compressor(file_size);

		for (const auto & chunk : chunks)
			chunk.compress(compressor);

		compressor.stop();

		std::filesystem::resize_file(path, 1 + compressor.used());
	}

	inline void write(const std::path_like auto & path) const
	{
		utils::blank_file(path, file_size);
		auto file = mio::mmap_sink(path);
		auto pos = file.data();

		auto capacity = file_size;
		for (const auto & chunk : chunks)
		{
			auto written = chunk.write(pos, capacity);
			pos += written;
			capacity -= written;
		}
	}
};

}

#endif
