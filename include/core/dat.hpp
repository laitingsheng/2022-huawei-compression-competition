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

	template<char sep, size_t mantissa_width, size_t index>
	[[nodiscard]]
	inline const char * parse_cell(const char * pos, size_t & size)
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

	template<size_t ... indices>
	[[nodiscard]]
	inline const char * parse_consecutive(std::index_sequence<indices ...>, const char * pos, size_t & size)
	{
		return ((pos = parse_cell<' ', 5, indices + 1>(pos, size)), ...);
	}

	template<char sep, size_t mantissa_width, size_t index>
	[[nodiscard]]
	inline char * write_cell(size_t line, char * pos, size_t & capacity) const
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

	template<size_t ... indices>
	[[nodiscard]]
	inline char * write_consecutive(std::index_sequence<indices ...>, size_t line, char * pos, size_t & capacity) const
	{
		return ((pos = write_cell<' ', 5, indices + 1>(line, pos, capacity)), ...);
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
		for (auto & sign : re.signs)
		{
			sign.resize(line_count);
			auto read = decompressor(pos, size, sign);
			pos += read;
			size -= read;
		}
		for (auto & column : re.columns)
		{
			std::vector<uint64_t> buffer(line_count);
			auto read = decompressor(pos, size, buffer);
			pos += read;
			size -= read;
			column = utils::seq::diff::reconstruct(buffer);
		}

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
			pos = re.parse_cell<' ', 3, 0>(pos, size);
			pos = re.parse_consecutive(std::make_index_sequence<69>(), pos, size);
			pos = re.parse_cell<'\r', 5, 70>(pos, size);

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
		for (const auto & sign : signs)
		{
			auto written = compressor(pos, capacity, sign);
			total += written;
			pos += written;
			capacity -= written;
		}
		for (const auto & column : columns)
		{
			auto written = compressor(pos, capacity, utils::seq::diff::construct(column));
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
			pos = write_cell<' ', 3, 0>(line, pos, capacity);
			pos = write_consecutive(std::make_index_sequence<69>(), line, pos, capacity);
			pos = write_cell<'\r', 5, 70>(line, pos, capacity);

			*pos++ = '\n';
			--capacity;
		}

		if (capacity > 0)
			throw std::runtime_error("unexpected uncompressed size");
	}
};

}

#endif
