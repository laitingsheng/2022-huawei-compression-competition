#ifndef __UTILS_FL2_HPP__
#define __UTILS_FL2_HPP__

#include <cstddef>

#include <concepts>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <fmt/core.h>
#include <fmt/compile.h>
#include <fast-lzma2/fast-lzma2.h>

#include "./traits.hpp"

namespace utils
{

namespace fl2
{

#define CHECK_FL2_RETURN(__E, __FM, ...) \
if (auto ret = __E; FL2_isError(ret)) \
[[unlikely]] \
	throw std::runtime_error(fmt::format( \
		FMT_STRING(__FM " ({})"), \
		__VA_ARGS__ __VA_OPT__(,) \
		FL2_getErrorName(ret) \
	));

class compressor final
{
	FL2_CCtx * _ctx;
	int _level;
public:
	explicit compressor(int level = FL2_maxHighCLevel()) : _ctx(FL2_createCCtx()), _level(level)
	{
		if (_ctx == nullptr)
			throw std::bad_alloc();

		CHECK_FL2_RETURN(
			FL2_CCtx_setParameter(_ctx, FL2_p_compressionLevel, level),
			"failed to set compression level"
		)
		CHECK_FL2_RETURN(
			FL2_CCtx_setParameter(_ctx, FL2_p_highCompression, true),
			"failed to set high compression"
		)
		CHECK_FL2_RETURN(
			FL2_CCtx_setParameter(_ctx, FL2_p_strategy, FL2_ultra),
			"failed to set strategy"
		)
	}

	compressor(const compressor &) = delete;
	compressor(compressor &&) = delete;

	~compressor() noexcept
	{
		FL2_freeCCtx(_ctx);
	}

	compressor & operator=(const compressor &) = delete;
	compressor & operator=(compressor &&) = delete;

	template<std::integral T>
	[[nodiscard]]
	inline size_t operator()(char * pos, size_t capacity, const std::vector<T> & content)
	{
		size_t written = FL2_compressCCtx(
			_ctx,
			pos + sizeof(size_t), capacity - sizeof(size_t),
			content.data(), content.size() * sizeof(T),
			_level
		);
		CHECK_FL2_RETURN(written, "failed to compress data")

		*reinterpret_cast<size_t *>(pos) = written;

		return written + sizeof(size_t);
	}
};

class decompressor final
{
	FL2_DCtx * _ctx;
public:
	explicit decompressor() : _ctx(FL2_createDCtx())
	{
		if (_ctx == nullptr)
			throw std::bad_alloc();
	}

	decompressor(const decompressor &) = delete;
	decompressor(decompressor &&) = delete;

	~decompressor() noexcept
	{
		CHECK_FL2_RETURN(
			FL2_freeDCtx(_ctx),
			"failed to free decompression context"
		)
	}

	decompressor & operator=(const decompressor &) = delete;
	decompressor & operator=(decompressor &&) = delete;

	template<std::integral T>
	[[nodiscard]]
	inline size_t operator()(const char * pos, size_t size, std::vector<T> & output)
	{
		auto compressed = *reinterpret_cast<const size_t *>(pos);
		if (compressed > size - sizeof(size_t))
			throw std::runtime_error("corrupted compressed file");

		CHECK_FL2_RETURN(
			FL2_decompressDCtx(
				_ctx,
				output.data(), output.size() * sizeof(T),
				pos + sizeof(size_t), compressed
			),
			"failed to decompress data"
		)

		return compressed + sizeof(size_t);
	}
};

}

template<>
struct is_compressor<fl2::compressor> : std::true_type {};

template<>
struct is_decompressor<fl2::decompressor> : std::true_type {};

}

#endif
