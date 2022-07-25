#ifndef __CORE_CFD_HPP__
#define __CORE_CFD_HPP__

#include <cstddef>
#include <cstdint>

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <ctre.hpp>
#include <fmt/core.h>
#include <mio/mmap.hpp>
#include <MurmurHash3.h>

#include "../utils/utils.hpp"

namespace core
{

template<std::integral T>
class simple_array_ref final
{
	const T * _data;
	size_t _size;
public:
	constexpr simple_array_ref() noexcept : _data(nullptr), _size(0) {}
	constexpr simple_array_ref(const T * data, size_t size) noexcept : _data(data), _size(size) {}

	constexpr ~simple_array_ref() noexcept = default;

	constexpr simple_array_ref(const simple_array_ref &) = default;
	constexpr simple_array_ref(simple_array_ref &&) noexcept = default;

	constexpr simple_array_ref & operator=(const simple_array_ref &) & = default;
	constexpr simple_array_ref & operator=(simple_array_ref &&) & noexcept = default;

	simple_array_ref & operator=(const simple_array_ref &) && = delete;
	simple_array_ref & operator=(simple_array_ref &&) && = delete;

	constexpr operator bool() const noexcept
	{
		return _data && _size;
	}

	[[nodiscard]]
	constexpr bool operator==(const simple_array_ref & other) const noexcept
	{
		if (_size != other._size)
			return false;
		if (_data == other._data)
			return true;
		for (size_t i = 0; i < _size; ++i)
			if (_data[i] != other._data[i])
				return false;
		return true;
	}

	[[nodiscard]]
	constexpr const T * data() const noexcept
	{
		return _data;
	}

	[[nodiscard]]
	constexpr size_t hash() const noexcept
	{
		uint64_t value[2] {};
		MurmurHash3_x64_128(_data, _size * sizeof(T), _size, value);
		return value[0] ^ value[1];
	}

	[[nodiscard]]
	constexpr size_t size() const noexcept
	{
		return _size;
	}
};

}

namespace std
{

template<std::integral T>
struct hash<core::simple_array_ref<T>> final
{
	constexpr hash() noexcept = default;
	constexpr ~hash() noexcept = default;

	constexpr hash(const hash &) = default;
	constexpr hash(hash &&) noexcept = default;

	inline constexpr size_t operator()(const core::simple_array_ref<T> & ref) const noexcept
	{
		return ref.hash();
	}
};

}

namespace core
{

template<std::integral T, typename Allocator = std::allocator<T>>
class cfd final
{
	inline static size_t parse_integer(const std::string_view & fragment)
	{
		size_t result;
		auto start = fragment.data(), end = start + fragment.size();
		if (auto [ptr, ec] = std::from_chars(start, end, result); ec == std::errc() && ptr == end)
		[[likely]]
			return result;
		throw std::runtime_error("invalid integer string");
	}

	inline static std::tuple<size_t, size_t, size_t, size_t> parse_path(const std::string_view & path)
	{
		static constexpr auto MATCHER = ctre::match<R"""(^.+?_(\d+)[Xx](\d+)[Xx](\d+)[Xx](\d+)\.raw$)""">;
		if(auto [whole, time, level, latitude, longitude] = MATCHER(path); whole)
		[[likely]]
			return {
				parse_integer(longitude),
				parse_integer(latitude),
				parse_integer(level),
				parse_integer(time)
			};
		throw std::runtime_error("invalid input path");
	}

	size_t _longitude, _latitude, _level, _time;

	Allocator allocator;
	T * _data;
	size_t _size;
public:
	static auto from_file(const std::string & path)
	{
		auto [longitude, latitude, level, time] = parse_path(path);

		auto source = mio::mmap_source(path);
		cfd ret(source.size() / sizeof(T), longitude, latitude, level, time);
		auto raw_data = reinterpret_cast<const T *>(source.data());
		T * dest = ret._data;
		for (
			size_t longitude_i = 0, raw_offset = 0, local_offset = 0, local_time_stride = longitude * latitude * level;
			longitude_i < longitude;
			++longitude_i
		)
			for (size_t latitude_i = 0; latitude_i < latitude; ++latitude_i)
				for (size_t level_i = 0; level_i < level; ++level_i, ++local_offset)
					for (
						size_t time_i = 0, stride_offset = 0;
						time_i < time;
						++time_i, ++raw_offset, stride_offset += local_time_stride
					)
						dest[local_offset + stride_offset] = raw_data[raw_offset];
		return ret;
	}

	cfd(size_t size, size_t longitude, size_t latitude, size_t level, size_t time) :
		_longitude(longitude),
		_latitude(latitude),
		_level(level),
		_time(time),
		allocator(),
		_data(std::allocator_traits<Allocator>::allocate(allocator, size)),
		_size(size)
	{
		if (longitude * latitude * level * time != size)
		[[unlikely]]
			throw std::runtime_error("shape mismatched");
	}

	~cfd()
	{
		std::allocator_traits<Allocator>::deallocate(allocator, _data, _size);
	}

	cfd(const cfd &) = delete;
	cfd(cfd &&) noexcept = default;

	cfd & operator=(const cfd &) = delete;
	cfd & operator=(cfd &&) = delete;
private:
	enum class data_type : uint8_t
	{
		UINT8,
		UINT16,
		UINT32,
		UINT64
	};

	inline static void write(uint8_t * pos, data_type dest_type, std::integral auto value)
	{
		switch (dest_type)
		{
			case data_type::UINT8:
				*pos = value;
				return;
			case data_type::UINT16:
				*reinterpret_cast<uint16_t *>(pos) = value;
				return;
			case data_type::UINT32:
				*reinterpret_cast<uint32_t *>(pos) = value;
				return;
			case data_type::UINT64:
				*reinterpret_cast<uint64_t *>(pos) = value;
				return;
			default:
			[[unlikely]]
				throw std::logic_error("invalid data type");
		}
	}

	inline static std::tuple<data_type, size_t> guess_type(std::unsigned_integral auto value) noexcept
	{
		if (value <= std::numeric_limits<uint8_t>::max())
			return { data_type::UINT8, sizeof(uint8_t) };
		if (value <= std::numeric_limits<uint16_t>::max())
			return { data_type::UINT16, sizeof(uint16_t) };
		if (value <= std::numeric_limits<uint32_t>::max())
			return { data_type::UINT32, sizeof(uint32_t) };
		return { data_type::UINT64, sizeof(uint64_t) };
	}
public:
	void compress_to_file(const std::string & path) const
	{
		std::unordered_map<simple_array_ref<T>, std::vector<size_t>> duplications;
		size_t max_count = 0, index = 0;
		for (size_t time_i = 0, offset = 0; time_i < _time; ++time_i)
			for (size_t longitude_i = 0; longitude_i < _longitude; ++longitude_i)
				for (size_t latitude_i = 0; latitude_i < _latitude; ++latitude_i, ++index, offset += _level)
				{
					auto & record = duplications[{_data + offset, _level}];
					record.push_back(index);
					max_count = std::max(max_count, record.size());
				}

		auto [longitude_type, longitude_width] = guess_type(_longitude);
		auto [latitude_type, latitude_width] = guess_type(_latitude);
		auto [level_type, level_width] = guess_type(_level);
		auto [time_type, time_width] = guess_type(_time);

		auto [segment_count_type, segment_count_width] = guess_type(max_count);
		auto [segment_index_type, segment_index_width] = guess_type(index);

		auto segment_length = _level * sizeof(T);
		// let `n` denotes the number of segments
		// let `sc` denotes the integer width of the maximum number of segments (segment_count_width)
		// let `si` denotes the integer width of each individual index during compression (segment_index_width)
		// let `l` represents the total integer width of a segment (segment_length)
		// the compression of repetitions of a segment is meaningful
		// (does not incur an inflation) iff
		// `l + sc + n * si <= n * l` => `n >= (l + sc) / (l - si)`
		// since all of these variables are positive numbers, the rhs of the expression can be further rewritten as
		// `ceil((l + sc) / (l - si))`
		// which can be further transformed into
		// `floor((l + sc + (l - si - 1)) / (l - si))` => `floor((2 * l + sc - si - 1) / (l - si))`
		// for efficient computation
		auto keep_threshold = ((segment_length << 1) + segment_count_width - segment_index_width - 1) /
			(segment_length - segment_index_width);

		std::vector<simple_array_ref<T>> raw_segments(index);
		size_t raw_segments_count = 0, indices_count = 0;
		for (auto it = duplications.begin(); it != duplications.end();)
			if (auto & [ref, indices] = *it; indices.size() < keep_threshold)
			{
				raw_segments_count += indices.size();
				for (auto i : indices)
					raw_segments[i] = ref;
				it = duplications.erase(it);
			}
			else
			{
				indices_count += indices.size();
				++it;
			}

		auto duplication_count = duplications.size();
		auto [duplication_count_type, duplication_count_width] = guess_type(duplication_count);

		auto [raw_segments_count_count_type, raw_segments_count_width] = guess_type(raw_segments_count);

		auto dest_size = 2 +
			longitude_width + latitude_width + level_width + time_width +
			duplication_count_width +
			duplication_count * (segment_length + segment_count_width) + indices_count * segment_index_width +
			raw_segments_count_width +
			raw_segments_count * segment_length;

		utils::blank_file(path, dest_size);
		auto sink = mio::mmap_sink(path);

		auto start_pos = reinterpret_cast<uint8_t *>(sink.data());
		*start_pos++ = static_cast<uint8_t>(longitude_type) << 6 |
			static_cast<uint8_t>(latitude_type) << 4 |
			static_cast<uint8_t>(level_type) << 2 |
			static_cast<uint8_t>(time_type);
		*start_pos++ = static_cast<uint8_t>(segment_count_type) << 6 |
			static_cast<uint8_t>(segment_index_type) << 4 |
			static_cast<uint8_t>(duplication_count_type) << 2 |
			static_cast<uint8_t>(raw_segments_count_count_type);

		write(start_pos, longitude_type, _longitude);
		start_pos += longitude_width;
		write(start_pos, latitude_type, _latitude);
		start_pos += latitude_width;
		write(start_pos, level_type, _level);
		start_pos += level_width;
		write(start_pos, time_type, _time);
		start_pos += time_width;

		write(start_pos, duplication_count_type, duplication_count);
		start_pos += duplication_count_width;
		for (auto & [ref, indices] : duplications)
		{
			std::copy(ref.data(), ref.data() + _level, reinterpret_cast<T *>(start_pos));
			start_pos += segment_length;

			write(start_pos, segment_count_type, indices.size());
			start_pos += segment_count_width;
			for (auto i : indices)
			{
				write(start_pos, segment_index_type, i);
				start_pos += segment_index_width;
			}
		}

		write(start_pos, raw_segments_count_count_type, raw_segments_count);
		start_pos += raw_segments_count_width;
		for (auto & ref : raw_segments)
			if (ref)
			{
				std::copy(ref.data(), ref.data() + _level, reinterpret_cast<T *>(start_pos));
				start_pos += segment_length;
			}
	}
};

}

#endif
