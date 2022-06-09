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

#include <mio/mmap.hpp>

#include "../std.hpp"
#include "../utils/seq.hpp"
#include "../utils/traits.hpp"

namespace core::dat
{

class data final
{
	std::array<std::vector<uint8_t>, 71> signs;
	std::array<std::vector<uint64_t>, 71> columns;
	size_t line_count, file_size;

	explicit data() : columns(), line_count(0), file_size(0) {}

	template<char sep, size_t mantissa_width>
	[[nodiscard]]
	inline const char * parse_cell(const char * pos, size_t & size, size_t index)
	{
		constexpr size_t max_integer_width = std::numeric_limits<uint64_t>::digits10 - mantissa_width;

		bool sign;
		size_t integer_width;
		uint64_t number;
		char c = *pos++;
		if (c == '-')
		{
			sign = true;
			integer_width = 0;
			number = 0;
		}
		else if (isdigit(c))
		{
			sign = false;
			integer_width = 1;
			number = c - '0';
		}
		else
		[[unlikely]]
			throw std::runtime_error("invalid leading character of the floating point number");

		signs[index].push_back(sign);

		for (c = *pos++; integer_width < max_integer_width && isdigit(c); c = *pos++, ++integer_width)
			number = number * 10 + (c - '0');

		if (isdigit(c))
		[[unlikely]]
			throw std::runtime_error("insufficient length for the floating point integer part");

		if (c != '.')
		[[unlikely]]
			throw std::runtime_error("unprocessable integer part of the floating point");

		size_t parsed_mantissa_width = 0;
		for (c = *pos++; parsed_mantissa_width < mantissa_width && isdigit(c); c = *pos++, ++parsed_mantissa_width)
			number = number * 10 + (c - '0');

		if (parsed_mantissa_width < mantissa_width)
		[[unlikely]]
			throw std::runtime_error("insufficient mantissa parsed from the input");

		if (isdigit(c))
		[[unlikely]]
			throw std::runtime_error("insufficient length for the floating point mantissa part");

		if (c != sep)
		[[unlikely]]
			throw std::runtime_error("invalid trailing character after the floating point number");

		columns[index].push_back(number);
		size -= sign + integer_width + 1 + mantissa_width + 1;
		return pos;
	}

	template<char sep, size_t mantissa_width>
	[[nodiscard]]
	inline char * write_cell(char * pos, size_t & capacity, size_t line, size_t index) const
	{
		auto number = columns[index][line];
		std::vector<char> buffer;
		buffer.reserve(std::numeric_limits<uint64_t>::digits10 + 3);
		buffer.push_back(sep);
		for (size_t i = 0; i < mantissa_width; ++i)
		{
			buffer.push_back('0' + number % 10);
			number /= 10;
		}
		buffer.push_back('.');
		if (number > 0)
		{
			while (number > 0)
			{
				buffer.push_back('0' + number % 10);
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
		pos += 2 * sizeof(size_t);
		size -= 2 * sizeof(size_t);

		for (auto & column : re.columns)
			column.resize(line_count);
		for (auto & sign : re.signs)
			sign.resize(line_count);

		auto read = decompressor(pos, size, re.columns[0]);
		pos += read;
		size -= read;
		utils::seq::diff::reconstruct(re.columns[0]);

		for (size_t i = 1; i < 71; ++i)
		{
			read = decompressor(pos, size, re.columns[i]);
			pos += read;
			size -= read;
		}

		for (auto & sign : re.signs)
		{
			read = decompressor(pos, size, sign);
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
		static constexpr size_t leading_size = 1 + 2 * sizeof(size_t);

		size_t capacity = leading_size + 71 * (
			sizeof(size_t) +
			line_count * sizeof(uint64_t) +
			sizeof(size_t) +
			line_count * sizeof(uint8_t)
		);
		utils::blank_file(path, capacity);
		auto file = mio::mmap_sink(path);
		auto pos = file.data();

		*pos++ = static_cast<uint8_t>(utils::file_type::dat);
		auto constants = reinterpret_cast<size_t *>(pos);
		constants[0] = line_count;
		constants[1] = file_size;
		pos += 2 * sizeof(size_t);
		capacity -= leading_size;
		size_t total = leading_size;

		auto written = compressor(pos, capacity, utils::seq::diff::construct(columns[0]));
		total += written;
		pos += written;
		capacity -= written;

		for (size_t i = 1; i < 71; ++i)
		{
			written = compressor(pos, capacity, columns[i]);
			total += written;
			pos += written;
			capacity -= written;
		}

		for (const auto & sign : signs)
		{
			written = compressor(pos, capacity, sign);
			total += written;
			pos += written;
			capacity -= written;
		}

		std::filesystem::resize_file(path, total);
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
