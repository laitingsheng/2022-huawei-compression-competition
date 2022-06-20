#ifndef __UTILS_FL2_HPP__
#define __UTILS_FL2_HPP__

#include <cstddef>

#include <concepts>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <fmt/compile.h>
#include <fmt/core.h>
#include <fast-lzma2/fast-lzma2.h>

#include "./traits.hpp"

namespace utils
{

namespace fl2
{

#define CHECK_FL2_RETURN(__E, __FM, ...) \
if (auto ret = (__E); FL2_isError(ret)) \
[[unlikely]] \
	throw std::runtime_error(fmt::format( \
		FMT_COMPILE("failed to " __FM " ({})"), \
		__VA_ARGS__ __VA_OPT__(,) \
		FL2_getErrorName(ret) \
	));

class compressor final
{
	FL2_outBuffer buffer;
	bool streaming;
	FL2_CStream * _ctx;
public:
	explicit compressor() : _ctx(FL2_createCStream()), streaming(false)
	{
		if (_ctx == nullptr)
		[[unlikely]]
			throw std::bad_alloc();

		auto level = FL2_maxHighCLevel();
		CHECK_FL2_RETURN(
			FL2_CStream_setParameter(_ctx, FL2_p_compressionLevel, level),
			"set compression level to {}", level
		)
		CHECK_FL2_RETURN(
			FL2_CStream_setParameter(_ctx, FL2_p_highCompression, true),
			"enable high compression"
		)
		CHECK_FL2_RETURN(
			FL2_CStream_setParameter(_ctx, FL2_p_dictionaryLog, FL2_DICTLOG_MAX_64),
			"set dictionary log size to {}", FL2_DICTLOG_MAX_64
		)
		CHECK_FL2_RETURN(
			FL2_CStream_setParameter(_ctx, FL2_p_literalCtxBits, FL2_LC_MAX),
			"set number of literal context bits to {}", FL2_LC_MAX
		)
		CHECK_FL2_RETURN(
			FL2_CStream_setParameter(_ctx, FL2_p_posBits, FL2_PB_MAX),
			"set number of position bits to {}", FL2_PB_MAX
		)
		CHECK_FL2_RETURN(
			FL2_CStream_setParameter(_ctx, FL2_p_strategy, FL2_ultra),
			"set strategy to {}", FL2_ultra
		)
	}

	compressor(const compressor &) = delete;
	compressor(compressor &&) = delete;

	~compressor() noexcept
	{
		if (streaming)
			stop();
		FL2_freeCStream(_ctx);
	}

	compressor & operator=(const compressor &) = delete;
	compressor & operator=(compressor &&) = delete;

	inline void operator()(FL2_inBuffer & input)
	{
		if (!streaming)
		[[unlikely]]
			throw std::runtime_error("compression stream should be started before using it");

		CHECK_FL2_RETURN(FL2_compressStream(_ctx, &buffer, &input), "compress data")
		else if (ret)
		[[unlikely]]
			throw std::runtime_error("insufficient output buffer capacity");

		if (buffer.pos == buffer.size)
			streaming = false;
	}

	inline void operator()(const std::byte * bytes, size_t size)
	{
		FL2_inBuffer input {
			.src = bytes,
			.size = size,
			.pos = 0
		};
		operator()(input);
	}

	template<std::integral T>
	inline bool operator()(const T & content)
	{
		fmt::print("test\n");
		operator()(reinterpret_cast<const std::byte *>(&content), sizeof(T));
		fmt::print("test\n");
	}

	template<std::integral T, size_t N>
	inline void operator()(const std::array<T, N> & content)
	{
		operator()(reinterpret_cast<const std::byte *>(content.data()), N * sizeof(T));
	}

	template<std::integral T>
	inline void operator()(const std::vector<T> & content)
	{
		operator()(reinterpret_cast<const std::byte *>(content.data()), content.size() * sizeof(T));
	}

	[[nodiscard]]
	inline bool is_streaming() const
	{
		return streaming;
	}

	inline void start(std::byte * bytes, size_t capacity)
	{
		if (streaming)
		[[unlikely]]
			throw std::runtime_error("compression stream already started");

		CHECK_FL2_RETURN(
			FL2_initCStream(_ctx, 0),
			"initialise compression stream"
		)

		streaming = true;
		buffer = {
			.dst = bytes,
			.size = capacity,
			.pos = 0
		};
	}

	inline void stop()
	{
		CHECK_FL2_RETURN(FL2_endStream(_ctx, &buffer), "end compression stream")

		streaming = false;
	}

	[[nodiscard]]
	inline size_t used() const
	{
		return buffer.pos;
	}
};

class decompressor final
{
	FL2_inBuffer buffer;
	bool streaming;
	FL2_DStream * _ctx;
public:
	explicit decompressor() : _ctx(FL2_createDStream()), streaming(false)
	{
		if (_ctx == nullptr)
			throw std::bad_alloc();
	}

	decompressor(const decompressor &) = delete;
	decompressor(decompressor &&) = delete;

	~decompressor() noexcept
	{
		if (streaming)
			stop();
		CHECK_FL2_RETURN(
			FL2_freeDStream(_ctx),
			"free decompression stream"
		)
	}

	decompressor & operator=(const decompressor &) = delete;
	decompressor & operator=(decompressor &&) = delete;

	inline void operator()(FL2_outBuffer & output)
	{
		if (!streaming)
		[[unlikely]]
			throw std::runtime_error("decompression stream should be started before using it");

		CHECK_FL2_RETURN(FL2_decompressStream(_ctx, &output, &buffer), "decompress data")
		else if (!ret)
			streaming = false;
	}

	inline void operator()(std::byte * bytes, size_t size)
	{
		FL2_outBuffer output {
			.dst = bytes,
			.size = size,
			.pos = 0
		};
		operator()(output);
	}

	template<std::integral T>
	inline void operator()(T & content)
	{
		operator()(reinterpret_cast<std::byte *>(&content), sizeof(T));
	}

	template<std::integral T, size_t N>
	inline void operator()(std::array<T, N> & content)
	{
		operator()(reinterpret_cast<std::byte *>(content.data()), N * sizeof(T));
	}

	template<std::integral T>
	inline void operator()(std::vector<T> & content)
	{
		operator()(reinterpret_cast<std::byte *>(content.data()), content.size() * sizeof(T));
	}

	[[nodiscard]]
	inline bool is_streaming() const
	{
		return streaming;
	}

	inline void start(const std::byte * bytes, size_t size)
	{
		if (streaming)
		[[unlikely]]
			throw std::runtime_error("decompression stream already started");

		CHECK_FL2_RETURN(FL2_initDStream(_ctx), "initialise decompression stream")

		streaming = true;
		buffer = {
			.src = bytes,
			.size = size,
			.pos = 0
		};
	}

	inline void stop()
	{
		if (buffer.pos != buffer.size)
		[[unlikely]]
			throw std::runtime_error("decompression stream not fully consumed");

		streaming = false;
	}
};

}

template<>
struct is_compressor<fl2::compressor> : std::true_type {};

template<>
struct is_decompressor<fl2::decompressor> : std::true_type {};

}

#endif
