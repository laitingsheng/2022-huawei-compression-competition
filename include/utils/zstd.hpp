#ifndef __UTILS_ZSTD_HPP__
#define __UTILS_ZSTD_HPP__

#include <cstddef>
#include <cstdint>

#include <array>
#include <concepts>
#include <string_view>
#include <vector>

#include <fmt/core.h>
#include <fmt/compile.h>
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd/zstd.h>
#define ZDICT_STATIC_LINKING_ONLY
#include <zstd/zdict.h>
#include <zstd/zstd_errors.h>

#include "./traits.hpp"

namespace utils
{

namespace zstd
{

#define CHECK_ZSTD_RETURN(__S, __FM, ...) \
if (auto ret = __S; ZSTD_isError(ret)) \
[[unlikely]] \
	throw std::runtime_error(fmt::format( \
		FMT_STRING(__FM " ({})"), \
		__VA_ARGS__ __VA_OPT__(,) \
		ZSTD_getErrorString(ZSTD_getErrorCode(ret)) \
	));

class compressor final
{
	ZSTD_CCtx * _ctx;
	int _level;
public:
	explicit compressor() : compressor(ZSTD_maxCLevel()) {}

	explicit compressor(int level) : _ctx(ZSTD_createCCtx()), _level(level)
	{
		if (_ctx == nullptr)
			throw std::bad_alloc();

		CHECK_ZSTD_RETURN(
			ZSTD_CCtx_setParameter(_ctx, ZSTD_c_compressionLevel, _level),
			"failed to set compression level"
		)
		CHECK_ZSTD_RETURN(
			ZSTD_CCtx_setParameter(_ctx, ZSTD_c_strategy, ZSTD_btultra2),
			"failed to set strategy"
		)
	}

	compressor(const compressor &) = delete;
	compressor(compressor &&) = delete;

	~compressor() noexcept
	{
		CHECK_ZSTD_RETURN(
			ZSTD_freeCCtx(_ctx),
			"failed to free compression context"
		)
	}

	compressor & operator=(const compressor &) = delete;
	compressor & operator=(compressor &&) = delete;

	template<std::integral T, std::unsigned_integral SizeT>
	[[nodiscard]]
	[[using gnu : always_inline, hot]]
	inline size_t process(char * pos, size_t capacity, const std::vector<T> & content)
	{
		size_t written = ZSTD_compressCCtx(
			_ctx,
			pos + sizeof(SizeT), capacity - sizeof(SizeT),
			content.data(), content.size() * sizeof(T),
			_level
		);
		CHECK_ZSTD_RETURN(written, "failed to compress data")

		if constexpr (!std::is_same_v<SizeT, size_t>)
			if (written > std::numeric_limits<SizeT>::max())
				throw std::runtime_error("compressed data is larger than expected");

		*reinterpret_cast<SizeT *>(pos) = written;

		return written + sizeof(SizeT);
	}

	template<std::integral T, std::unsigned_integral SizeT>
	[[nodiscard]]
	[[using gnu : always_inline, hot]]
	inline size_t process(char * pos, size_t capacity, const std::vector<T> & content, const std::string_view & dict)
	{
		size_t written = ZSTD_compress_usingDict(
			_ctx,
			pos + sizeof(SizeT), capacity - sizeof(SizeT),
			content.data(), content.size() * sizeof(T),
			dict.data(), dict.size(),
			_level
		);
		CHECK_ZSTD_RETURN(written, "failed to compress data")

		if constexpr (!std::is_same_v<SizeT, size_t>)
			if (written > std::numeric_limits<SizeT>::max())
				throw std::runtime_error("compressed data is larger than expected");

		*reinterpret_cast<SizeT *>(pos) = written;

		return written + sizeof(SizeT);
	}
};

template<std::integral T, size_t segment_count, size_t max_dict_size>
[[nodiscard]]
[[using gnu : always_inline, hot]]
inline static std::vector<uint8_t> train(const std::vector<T> & data)
{
	std::vector<uint8_t> dict(max_dict_size);

	auto sample_sizes = std::vector<size_t>(data.size(), sizeof(T));
	auto size = ZDICT_trainFromBuffer_cover(
		dict.data(), max_dict_size,
		data.data(), sample_sizes.data(), data.size(),
		{
			.k = sizeof(T) * segment_count,
			.d = sizeof(T),
			.steps = 100,
			.nbThreads = 1,
			.splitPoint = 1.0,
			.shrinkDict = 0,
			.shrinkDictMaxRegression = 0,
			.zParams {
				.compressionLevel = ZSTD_maxCLevel(),
				.notificationLevel = 0,
				.dictID = 0
			}
		}
	);

	if (ZDICT_isError(size))
		throw std::runtime_error(fmt::format(
			"failed to train dictionary ({})",
			ZDICT_getErrorName(size)
		));

	dict.resize(size);
	return dict;
}

class decompressor final
{
	ZSTD_DCtx * _ctx;
public:
	explicit decompressor() : _ctx(ZSTD_createDCtx())
	{
		if (_ctx == nullptr)
			throw std::bad_alloc();
	}

	decompressor(const decompressor &) = delete;
	decompressor(decompressor &&) = delete;

	~decompressor()
	{
		CHECK_ZSTD_RETURN(
			ZSTD_freeDCtx(_ctx),
			"failed to free decompression context"
		)
	}

	decompressor & operator=(const decompressor &) = delete;
	decompressor & operator=(decompressor &&) = delete;

	template<std::integral T, std::unsigned_integral SizeT>
	[[nodiscard]]
	[[using gnu : always_inline, hot]]
	inline size_t process(const char * pos, size_t size, std::vector<T> & output)
	{
		auto compressed = *reinterpret_cast<const SizeT *>(pos);
		if (compressed > size - sizeof(SizeT))
			throw std::runtime_error("corrupted compressed file");

		CHECK_ZSTD_RETURN(
			ZSTD_decompressDCtx(
				_ctx,
				output.data(), output.size() * sizeof(T),
				pos + sizeof(SizeT), compressed
			),
			"failed to decompress data"
		)

		return compressed + sizeof(SizeT);
	}

	template<std::integral T, std::unsigned_integral SizeT>
	[[nodiscard]]
	[[using gnu : always_inline, hot]]
	inline size_t process(const char * pos, size_t size, std::vector<T> & output, const std::string_view & dict)
	{
		auto compressed = *reinterpret_cast<const SizeT *>(pos);
		if (compressed > size - sizeof(SizeT))
			throw std::runtime_error("corrupted compressed file");

		CHECK_ZSTD_RETURN(
			ZSTD_decompress_usingDict(
				_ctx,
				output.data(), output.size() * sizeof(T),
				pos + sizeof(SizeT), compressed,
				dict.data(), dict.size()
			),
			"failed to decompress data"
		)

		return compressed + sizeof(SizeT);
	}
};

}

template<>
struct is_compressor<zstd::compressor> : std::true_type {};

template<>
struct is_decompressor<zstd::decompressor> : std::true_type {};

}

#endif
