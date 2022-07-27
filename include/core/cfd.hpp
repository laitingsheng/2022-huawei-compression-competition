#ifndef __CORE_CFD_HPP__
#define __CORE_CFD_HPP__

#include <climits>
#include <cstddef>
#include <cstdint>

#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <ctre.hpp>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <mio/mmap.hpp>

#include "../utils/utils.hpp"

namespace core
{

class bits_reader final
{
	const uint8_t * _pos;
	uint8_t _bits_left;
public:
	bits_reader(const uint8_t * pos) : _pos(pos), _bits_left(CHAR_BIT) {}

	~bits_reader() noexcept = default;

	bits_reader(const bits_reader &) = delete;
	bits_reader(bits_reader &&) = delete;

	bits_reader & operator=(const bits_reader &) = delete;
	bits_reader & operator=(bits_reader &&) = delete;

	template<std::integral I>
	inline void operator()(I & value)
	{
		operator()(value, std::numeric_limits<I>::digits);
	}

	template<std::signed_integral I>
	void operator()(I & value, size_t written_bits)
	{
		bool sign = *_pos & (1 << --_bits_left);
		if (!_bits_left)
		{
			++_pos;
			_bits_left = CHAR_BIT;
		}
		std::make_unsigned_t<I> tmp = 0;
		operator()(tmp, written_bits);
		value = sign ? (-tmp | std::numeric_limits<I>::min()) : tmp;
	}

	void operator()(std::unsigned_integral auto & value, size_t read_bits)
	{
		if (_bits_left != CHAR_BIT)
			if (read_bits >= _bits_left)
			{
				value = *_pos++ & ((1 << _bits_left) - 1);
				read_bits -= _bits_left;
				_bits_left = CHAR_BIT;
				if (!read_bits)
					return;
			}
			else
			{
				_bits_left -= read_bits;
				value = (*_pos >> _bits_left) & ((1 << read_bits) - 1);
				return;
			}
		else
			value = 0;

		while (read_bits >= CHAR_BIT)
		{
			value <<= CHAR_BIT;
			value |= *_pos++;
			read_bits -= CHAR_BIT;
		}

		if (read_bits)
		{
			value <<= read_bits;
			_bits_left -= read_bits;
			value |= *_pos >> _bits_left;
		}
	}
};

class bits_writer final
{
	uint8_t * _pos, _bits_left;
public:
	bits_writer(uint8_t * pos) : _pos(pos), _bits_left(CHAR_BIT) {}

	~bits_writer() noexcept = default;

	bits_writer(const bits_writer &) = delete;
	bits_writer(bits_writer &&) = delete;

	bits_writer & operator=(const bits_writer &) = delete;
	bits_writer & operator=(bits_writer &&) = delete;

	template<std::integral I>
	inline void operator()(I value)
	{
		operator()(value, std::numeric_limits<I>::digits);
	}

	template<std::signed_integral I>
	void operator()(I value, size_t written_bits)
	{
		--_bits_left;
		if (value < 0)
		{
			*_pos |= 1 << _bits_left;
			value = -value;
		}
		if (!_bits_left)
		{
			++_pos;
			*_pos = 0;
			_bits_left = CHAR_BIT;
		}
		operator()(std::make_unsigned_t<I>(value), written_bits);
	}

	void operator()(std::unsigned_integral auto value, size_t written_bits)
	{
		value &= (1 << written_bits) - 1;

		if (_bits_left != CHAR_BIT)
			if (written_bits >= _bits_left)
			{
				written_bits -= _bits_left;
				*_pos++ |= value >> written_bits;
				_bits_left = CHAR_BIT;
				if (!written_bits)
					return;
				value &= (1 << written_bits) - 1;
			}
			else
			{
				_bits_left -= written_bits;
				*_pos |= value << _bits_left;
				return;
			}

		while (written_bits >= CHAR_BIT)
		{
			written_bits -= CHAR_BIT;
			*_pos++ = value >> written_bits;
			value &= (1 << written_bits) - 1;
		}

		if (written_bits)
		{
			_bits_left -= written_bits;
			*_pos = value << _bits_left;
		}
	}
};

class cfd_time_series final
{
	int16_t _mean;
	uint8_t _bits_width;
	std::vector<int16_t> _diffs;
public:
	cfd_time_series(const int16_t * input, uint16_t size) : _bits_width(0), _diffs(size)
	{
		int64_t sum = 0;
		for (size_t i = 0; i < size; ++i)
			sum += (_diffs[i] = input[i]);
		_mean = sum / int64_t(size);

		for (size_t i = 0; i < size; ++i)
			_diffs[i] -= _mean;

		for (auto diff : _diffs)
			if (diff)
			{
				if (diff < 0)
					diff = -diff;
				uint16_t udiff = diff, mask = 1 << 14;
				if (udiff & mask)
				{
					_bits_width = 15;
					break;
				}
				else
				{
					uint8_t sbs = 14;
					udiff &= mask - 1;
					mask >>= 1;
					while (!(udiff & mask))
					{
						--sbs;
						udiff &= mask - 1;
						mask >>= 1;
					}
					_bits_width = std::max(_bits_width, sbs);
				}
			}
	}

	cfd_time_series(bits_reader & reader, uint16_t size) : _diffs(size)
	{
		reader(_mean);
		reader(_bits_width, 4);
		for (auto & diff : _diffs)
			reader(diff, _bits_width);
	}

	~cfd_time_series() noexcept = default;

	cfd_time_series(const cfd_time_series &) = delete;
	cfd_time_series(cfd_time_series &&) noexcept = default;

	cfd_time_series & operator=(const cfd_time_series &) = delete;
	cfd_time_series & operator=(cfd_time_series &&) = delete;

	size_t bits() const noexcept
	{
		return 16 + 4 + (1 + _bits_width) * _diffs.size();
	}

	void compress(bits_writer & writer) const
	{
		writer(_mean);
		writer(_bits_width, 4);
		for (auto diff : _diffs)
			writer(diff, _bits_width);
	}

	void reconstruct(int16_t * output) const
	{
		for (size_t i = 0; i < _diffs.size(); ++i)
			output[i] = _mean + _diffs[i];
	}
};

class cfd final
{
	template<std::unsigned_integral T>
	inline static T parse_integer(const std::string_view & fragment)
	{
		T result;
		auto start = fragment.data(), end = start + fragment.size();
		if (auto [ptr, ec] = std::from_chars(start, end, result); ec == std::errc() && ptr == end)
		[[likely]]
			return result;
		else
		[[unlikely]]
			throw std::runtime_error("invalid integer string");
	}

	inline static auto parse_path(const std::string_view & path)
	{
		static constexpr auto MATCHER = ctre::match<R"""(^.+?_(\d+)[Xx](\d+)[Xx](\d+)[Xx](\d+)\.raw$)""">;
		if(auto [whole, time, level, latitude, longitude] = MATCHER(path); whole)
		[[likely]]
			return std::tuple {
				parse_integer<uint8_t>(longitude),
				parse_integer<uint8_t>(latitude),
				parse_integer<uint8_t>(level),
				parse_integer<uint16_t>(time)
			};
		else
		[[unlikely]]
			throw std::runtime_error("invalid input path");
	}

	std::vector<cfd_time_series> _time_series;
	uint32_t _count;
	uint16_t _span;

	cfd(uint32_t count, uint16_t span) : _count(count), _span(span)
	{
		_time_series.reserve(count);
	}
public:
	static cfd from_file(const std::string & path)
	{
		auto [longitude, latitude, level, time] = parse_path(path);

		uint32_t count = longitude * latitude * level;
		uint16_t span = time;

		auto source = mio::mmap_source(path);
		if (source.size() != count * span * 2)
		[[unlikely]]
			throw std::logic_error("shape mismatched");

		cfd ret(count, span);
		auto ptr = reinterpret_cast<const int16_t *>(source.data());
		for (size_t i = 0; i < count; ++i, ptr += span)
			ret._time_series.emplace_back(ptr, span);
		return ret;
	}

	static cfd from_compressed(const std::string & path)
	{
		auto source = mio::mmap_source(path);
		bits_reader reader(reinterpret_cast<const uint8_t *>(source.data()));

		uint32_t count;
		uint16_t span;
		reader(count);
		reader(span);
		cfd ret(count, span);
		for (size_t i = 0; i < count; ++i)
			ret._time_series.emplace_back(reader, span);
		return ret;
	}

	~cfd() noexcept = default;

	cfd(const cfd &) = delete;
	cfd(cfd &&) noexcept = default;

	cfd & operator=(const cfd &) = delete;
	cfd & operator=(cfd &&) = delete;

	void compress_to_file(const std::string & path) const
	{
		size_t size = 0;
		for (const auto & ref : _time_series)
			size += ref.bits();

		size = 6 + ((size + 7) >> 3);
		utils::blank_file(path, size);
		auto sink = mio::mmap_sink(path);

		bits_writer writer(reinterpret_cast<uint8_t *>(sink.data()));
		writer(_count);
		writer(_span);
		for (const auto & ref : _time_series)
			ref.compress(writer);
	}

	void reconstruct(const std::string & path) const
	{
		size_t size = _count * _span * 2;
		utils::blank_file(path, size);
		auto sink = mio::mmap_sink(path);

		auto ptr = reinterpret_cast<int16_t *>(sink.data());
		for (const auto & ref : _time_series)
		{
			ref.reconstruct(ptr);
			ptr += _span;
		}
	}
};

}

#endif
