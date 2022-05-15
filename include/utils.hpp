#ifndef __UTILS_HPP__
#define __UTILS_HPP__

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <concepts>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fast-lzma2/fast-lzma2.h>
#include <fmt/core.h>
#include <lz4/lz4.h>
#include <lz4/lz4hc.h>

namespace std
{

template<typename T>
concept object = is_object_v<T>;

template<typename T>
concept reference = is_reference_v<T>;

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

template<reference Path>
struct is_path_like<Path> : is_path_like<remove_reference_t<Path>> {};

template<typename Path>
	requires is_const_v<Path>
struct is_path_like<Path> : is_path_like<remove_const_t<Path>> {};

template<typename Path>
	requires is_volatile_v<Path>
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
[[using gnu : always_inline, hot]]
inline void blank_file(Path && file_path, size_t size)
{
	std::ofstream file(std::forward<Path>(file_path), std::ios::binary);
	file.seekp(size - 1);
	file.write("", 1);
}

namespace buffer
{

// TODO: consider using `std::allocator` or `operator new[]` to support fine-grain memory management
class bytes final
{
	void * _memory;
	size_t _size;
	bool _managed;

	bytes(void * memory, size_t size, bool managed) noexcept : _memory(memory), _size(size), _managed(managed) {}
public:
	bytes(size_t size) : bytes(malloc(size), size, true) {}

	bytes(void * memory, size_t size) noexcept : bytes(memory, size, false) {}

	template<typename T>
	bytes(T * content, size_t count) noexcept : bytes(content, count * sizeof(T), false) {}

	template<typename T, size_t N>
	bytes(std::array<T, N> & array) noexcept : bytes(array.data(), array.size() * sizeof(T), false) {}

	template<typename T>
	bytes(std::vector<T> & vector) noexcept : bytes(vector.data(), vector.size() * sizeof(T), false) {}

	bytes(const bytes & other) : bytes(other._size)
	{
		memcpy(_memory, other._memory, other._size);
	}

	bytes(bytes && other) noexcept : bytes(other._memory, other._size, other._managed)
	{
		other._memory = nullptr;
		other._size = 0;
		other._managed = false;
	}

	~bytes() noexcept
	{
		if (_managed)
			free(_memory);
	}

	bytes & operator=(const bytes & other)
	{
		if (_managed)
			free(_memory);

		_memory = malloc(other._size);
		_size = other._size;
		_managed = true;

		memcpy(_memory, other._memory, other._size);

		return *this;
	}

	bytes & operator=(bytes && other) noexcept
	{
		if (_managed)
			free(_memory);

		_memory = other._memory;
		_size = other._size;
		_managed = other._managed;

		other._memory = nullptr;
		other._size = 0;
		other._managed = false;

		return *this;
	}

	template<std::object T>
	[[nodiscard]]
	[[using gnu : pure]]
	T * as() noexcept
	{
		return reinterpret_cast<T *>(_memory);
	}

	template<std::object T>
	[[nodiscard]]
	[[using gnu : pure]]
	const T * as() const noexcept
	{
		return reinterpret_cast<const T *>(_memory);
	}

	template<std::object T>
	[[nodiscard]]
	[[using gnu : pure]]
	size_t count() const noexcept
	{
		return _size / sizeof(T);
	}

	[[nodiscard]]
	[[using gnu : pure]]
	void * data() noexcept
	{
		return _memory;
	}

	[[nodiscard]]
	[[using gnu : pure]]
	const void * data() const noexcept
	{
		return _memory;
	}

	[[nodiscard]]
	[[using gnu : pure]]
	size_t size() const noexcept
	{
		return _size;
	}

	[[nodiscard]]
	[[using gnu : pure]]
	bool managed() const noexcept
	{
		return _managed;
	}
};

}

namespace counter
{

template<std::integral T, std::signed_integral DT, std::unsigned_integral CT>
class differential final
{
	T value, diff;
	CT count;
	std::vector<std::pair<T, CT>> counter;
public:
	differential() : value(0), diff(0), count(0), counter() {}

	void add(T new_value)
	{
		DT current_diff = DT(new_value) - DT(value);
		if constexpr (!std::is_same_v<T, DT>)
			current_diff &= std::numeric_limits<T>::max();

		if (current_diff == diff)
			++count;
		else
		{
			if (count)
				counter.emplace_back(diff, count);
			diff = T(current_diff);
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

template<std::integral T, std::unsigned_integral CT>
[[nodiscard]]
[[using gnu : always_inline, hot]]
inline char * write(char * pos, uint32_t & capacity, uint32_t & written, const std::vector<std::pair<T, CT>> & counter)
{
	if (4 + counter.size() * (sizeof(T) + sizeof(CT)) > capacity)
	{
		fmt::print(stderr, "Not enough space to serialise the counter.\n");
		return nullptr;
	}

	*reinterpret_cast<uint32_t *>(pos) = counter.size();
	pos += 4;
	written += 4;
	capacity -= 4;

	for (const auto & [value, count] : counter)
	{
		*reinterpret_cast<T *>(pos) = value;
		pos += sizeof(T);
		written += sizeof(T);
		capacity -= sizeof(T);
		*reinterpret_cast<CT *>(pos) = count;
		pos += sizeof(CT);
		written += sizeof(CT);
		capacity -= sizeof(CT);
	}

	return pos;
}

}

namespace fl2
{

class compressor final
{
	FL2_CCtx * ctx;
public:
	compressor() : ctx(FL2_createCCtx())
	{
		FL2_CCtx_setParameter(ctx, FL2_p_highCompression, 1);
		FL2_CCtx_setParameter(ctx, FL2_p_strategy, 3);
		FL2_CCtx_setParameter(ctx, FL2_p_literalCtxBits, FL2_LC_MAX);
		FL2_CCtx_setParameter(ctx, FL2_p_literalPosBits, FL2_LP_MAX);
		FL2_CCtx_setParameter(ctx, FL2_p_posBits, FL2_PB_MAX);
		FL2_CCtx_setParameter(ctx, FL2_p_compressionLevel, FL2_maxHighCLevel());
	}

	compressor(const compressor &) = delete;
	compressor(compressor &&) = delete;

	~compressor()
	{
		FL2_freeCCtx(ctx);
	}

	compressor & operator=(const compressor &) = delete;
	compressor & operator=(compressor &&) = delete;

	template<std::integral T>
	[[nodiscard]]
	[[using gnu : always_inline, hot]]
	inline char * operator()(char * pos, uint32_t & capacity, uint32_t & written, const std::vector<T> & data) noexcept
	{
		if (capacity < 4)
		{
			fmt::print(stderr, "Not enough space to compress the vector.\n");
			return nullptr;
		}

		size_t fl2_written = FL2_compressCCtx(
			ctx,
			pos + 4,
			capacity,
			data.data(),
			data.size() * sizeof(T),
			FL2_maxHighCLevel()
		);

		if (FL2_isError(fl2_written))
		{
			fmt::print(stderr, "Failed to compress the vector (LZMA2, {}).\n", FL2_getErrorName(fl2_written));
			return nullptr;
		}
		*reinterpret_cast<uint32_t *>(pos) = fl2_written;

		fl2_written += 4;
		written += fl2_written;
		capacity -= fl2_written;

		return pos + fl2_written;
	}
};

class decompressor final
{
	FL2_DCtx * ctx;
public:
	decompressor() : ctx(FL2_createDCtx()) {}

	decompressor(const decompressor &) = delete;
	decompressor(decompressor &&) = delete;

	~decompressor()
	{
		FL2_freeDCtx(ctx);
	}

	decompressor & operator=(const decompressor &) = delete;
	decompressor & operator=(decompressor &&) = delete;

	template<std::integral T>
	[[nodiscard]]
	[[using gnu : always_inline, hot]]
	inline const char * operator()(const char * pos, std::vector<T> & data) noexcept
	{
		uint32_t compressed = *reinterpret_cast<const uint32_t *>(pos), decompressed = data.size() * sizeof(T);
		size_t fl2_extracted = FL2_decompressDCtx(ctx, data.data(), decompressed, pos + 4, compressed);

		if (FL2_isError(fl2_extracted))
		{
			fmt::print(stderr, "Failed to decompress the vector (LZMA2, {}).\n", FL2_getErrorName(fl2_extracted));
			return nullptr;
		}
		if (fl2_extracted != decompressed)
		{
			fmt::print(stderr, "Failed to decompress the vector (LZMA2, {} vs {}).\n", fl2_extracted, decompressed);
			return nullptr;
		}
		return pos + compressed + 4;
	}
};

}

}

#endif
