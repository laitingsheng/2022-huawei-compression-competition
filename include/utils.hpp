#ifndef __UTILS_HPP__
#define __UTILS_HPP__

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <concepts>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fmt/core.h>
#include <zstd.h>
#include <zstd_errors.h>

namespace std
{

template<typename T>
concept object = is_object_v<T>;

template<typename T>
concept reference_type = is_reference_v<T>;

template<typename T>
concept const_type = is_const_v<T>;

template<typename T>
concept volatile_type = is_volatile_v<T>;

template<typename T>
struct is_char_type : false_type {};

template<>
struct is_char_type<char> : true_type {};

template<>
struct is_char_type<wchar_t> : true_type {};

template<>
struct is_char_type<char8_t> : true_type {};

template<>
struct is_char_type<char16_t> : true_type {};

template<>
struct is_char_type<char32_t> : true_type {};

template<typename T>
inline constexpr bool is_char_type_v = is_char_type<T>::value;

template<typename T>
concept char_type = is_char_type_v<T>;

template<typename Path>
struct is_path_like : false_type {};

template<reference_type Path>
struct is_path_like<Path> : is_path_like<remove_reference_t<Path>> {};

template<const_type Path>
struct is_path_like<Path> : is_path_like<remove_const_t<Path>> {};

template<volatile_type Path>
struct is_path_like<Path> : is_path_like<remove_volatile_t<Path>> {};

template<char_type C>
struct is_path_like<const C *> : true_type {};

template<char_type C>
struct is_path_like<C *> : true_type {};

template<char_type CharT, typename Traits, typename Allocator>
struct is_path_like<basic_string<CharT, Traits, Allocator>> : true_type {};

template<char_type CharT, typename Traits>
struct is_path_like<basic_string_view<CharT, Traits>> : true_type {};

template<>
struct is_path_like<filesystem::path> : true_type {};

template<typename Path>
inline constexpr bool is_path_like_v = is_path_like<Path>::value;

template<typename Path>
concept path_like = is_path_like_v<Path>;

}

namespace utils
{

template<std::path_like Path>
[[using gnu : always_inline]]
inline static void blank_file(Path && file_path, size_t size)
{
	std::ofstream file(std::forward<Path>(file_path), std::ios::binary);
	file.seekp(size - 1);
	file.write("", 1);
}

namespace counter
{

template<std::integral T, std::unsigned_integral SizeT>
class differential final
{
	T value, diff;
	SizeT count;
	std::vector<std::pair<T, SizeT>> counter;
public:
	differential() : value(0), diff(0), count(0), counter() {}

	void add(T new_value)
	{
		if (T current_diff = new_value - value; current_diff == diff)
			++count;
		else
		{
			if (count)
				counter.emplace_back(diff, count);
			diff = current_diff;
			count = 1;
		}
		value = new_value;
	}

	void commit()
	{
		if (count)
			counter.emplace_back(diff, count);
	}

	[[nodiscard]]
	[[using gnu : pure]]
	const auto & data() const
	{
		return counter;
	}

	[[nodiscard]]
	[[using gnu : pure]]
	auto size() const
	{
		return counter.size();
	}
};

template<std::integral T, std::unsigned_integral SizeT>
[[nodiscard]]
[[using gnu : always_inline]]
inline static size_t reconstruct_differential(const char * read_pos, std::vector<T> & vector)
{
	auto size = *reinterpret_cast<const SizeT *>(read_pos);
	read_pos += sizeof(SizeT);
	auto read = sizeof(SizeT);

	T value = 0;
	for (SizeT i = 0, index = 0; i < size; ++i)
	{
		auto diff = *reinterpret_cast<const T *>(read_pos);
		read_pos += sizeof(T);
		auto count = *reinterpret_cast<const SizeT *>(read_pos);
		read_pos += sizeof(SizeT);

		for (SizeT j = 0; j < count; ++j, ++index)
		{
			value += diff;
			vector[index] = value;
		}

		read += sizeof(T) + sizeof(SizeT);
	}

	return read;
}

template<std::integral T, std::unsigned_integral SizeT>
[[nodiscard]]
[[using gnu : always_inline]]
inline static size_t write(char * pos, size_t capacity, const std::vector<std::pair<T, SizeT>> & counter)
{
	size_t total = sizeof(SizeT) + counter.size() * (sizeof(T) + sizeof(SizeT));

	if (total > capacity)
		throw new std::runtime_error("not enough space to serialise the counter");

	*reinterpret_cast<SizeT *>(pos) = counter.size();
	pos += sizeof(SizeT);

	for (const auto & [value, count] : counter)
	{
		*reinterpret_cast<T *>(pos) = value;
		pos += sizeof(T);
		*reinterpret_cast<SizeT *>(pos) = count;
		pos += sizeof(SizeT);
	}

	return total;
}

}

namespace fl2
{

template<const std::string_view & DICT>
class compressor final
{
	ZSTD_CCtx * _ctx;
	int _level;
public:
	compressor() : compressor(ZSTD_maxCLevel()) {}

	compressor(int level) : _ctx(ZSTD_createCCtx()), _level(level)
	{
		ZSTD_CCtx_setParameter(_ctx, ZSTD_c_compressionLevel, _level);
		ZSTD_CCtx_setParameter(_ctx, ZSTD_c_strategy, ZSTD_btultra2);

		if constexpr (DICT.size())
			if (auto ret = ZSTD_CCtx_loadDictionary(_ctx, DICT.data(), DICT.size()); ZSTD_isError(ret))
				throw std::runtime_error(fmt::format(
					"failed to load dictionary for compression ({})",
					ZSTD_getErrorString(ZSTD_getErrorCode(ret))
				));
	}

	compressor(const compressor &) = delete;
	compressor(compressor &&) = delete;

	~compressor()
	{
		ZSTD_freeCCtx(_ctx);
	}

	compressor & operator=(const compressor &) = delete;
	compressor & operator=(compressor &&) = delete;

	template<std::integral T>
	[[nodiscard]]
	[[using gnu : always_inline, hot]]
	inline size_t operator()(char * pos, size_t capacity, const std::vector<T> & content)
	{
		size_t written = ZSTD_compressCCtx(
			_ctx,
			pos + sizeof(size_t),
			capacity - sizeof(size_t),
			content.data(),
			content.size() * sizeof(T),
			_level
		);

		if (ZSTD_isError(written))
			throw std::runtime_error(fmt::format(
				"compression failed ({})",
				ZSTD_getErrorString(ZSTD_getErrorCode(written))
			));

		*reinterpret_cast<size_t *>(pos) = written;

		return written + sizeof(size_t);
	}
};

template<const std::string_view & DICT>
class decompressor final
{
	ZSTD_DCtx * _ctx;
public:
	decompressor() : _ctx(ZSTD_createDCtx())
	{
		if constexpr (DICT.size())
			if (auto ret = ZSTD_DCtx_loadDictionary(_ctx, DICT.data(), DICT.size()); ZSTD_isError(ret))
				throw std::runtime_error(fmt::format(
					"failed to load dictionary for decompression ({})",
					ZSTD_getErrorString(ZSTD_getErrorCode(ret))
				));
	}

	decompressor(const decompressor &) = delete;
	decompressor(decompressor &&) = delete;

	~decompressor()
	{
		ZSTD_freeDCtx(_ctx);
	}

	decompressor & operator=(const decompressor &) = delete;
	decompressor & operator=(decompressor &&) = delete;

	template<std::integral T>
	[[nodiscard]]
	[[using gnu : always_inline, hot]]
	inline size_t operator()(const char * pos, std::vector<T> & output)
	{
		size_t compressed = *reinterpret_cast<const size_t *>(pos);

		size_t extracted = ZSTD_decompressDCtx(
			_ctx,
			output.data(),
			output.size() * sizeof(T),
			pos + sizeof(size_t),
			compressed
		);

		if (ZSTD_isError(extracted))
			throw std::runtime_error(fmt::format(
				"decompression failed ({})",
				ZSTD_getErrorString(ZSTD_getErrorCode(extracted))
			));

		return compressed + sizeof(size_t);
	}
};

}

}

#endif
