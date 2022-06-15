#ifndef __UTILS_BROTLI_HPP__
#define __UTILS_BROTLI_HPP__

#include <cstddef>
#include <cstdint>

#include <concepts>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <brotli/decode.h>
#include <brotli/encode.h>
#include <fmt/compile.h>
#include <fmt/core.h>

#include "./traits.hpp"

namespace utils
{

namespace brotli
{

#define CHECK_BROTLI_RETURN(__E, __FM, ...) \
if (auto __ret = (__E); !__ret) \
[[unlikely]] \
	throw std::runtime_error(fmt::format( \
		FMT_COMPILE("failed to " __FM " ({})"), \
		__VA_ARGS__ __VA_OPT__(,) \
		__ret \
	));

class compressor final
{
	bool streaming;
	uint8_t * out;
	size_t available_out;
	BrotliEncoderState * _ctx;
public:
	explicit compressor() : streaming(false) {}

	compressor(const compressor &) = delete;
	compressor(compressor &&) = delete;

	~compressor() noexcept
	{
		if (streaming)
			stop();
	}

	compressor & operator=(const compressor &) = delete;
	compressor & operator=(compressor &&) = delete;

	inline void start(std::byte * bytes, size_t capacity, size_t hint = 0)
	{
		if (streaming)
		[[unlikely]]
			throw std::runtime_error("compression stream already started");

		_ctx = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
		if (_ctx == nullptr)
			throw std::bad_alloc();

		CHECK_BROTLI_RETURN(
			BrotliEncoderSetParameter(_ctx, BROTLI_PARAM_QUALITY, BROTLI_MAX_QUALITY),
			"set compression quality to {}", BROTLI_MAX_QUALITY
		)
		CHECK_BROTLI_RETURN(
			BrotliEncoderSetParameter(_ctx, BROTLI_PARAM_LGWIN, BROTLI_LARGE_MAX_WINDOW_BITS),
			"set lgwin to {}", BROTLI_LARGE_MAX_WINDOW_BITS
		)
		CHECK_BROTLI_RETURN(
			BrotliEncoderSetParameter(_ctx, BROTLI_PARAM_LGBLOCK, BROTLI_MAX_INPUT_BLOCK_BITS),
			"set lgblock to {}", BROTLI_MAX_INPUT_BLOCK_BITS
		)
		CHECK_BROTLI_RETURN(
			BrotliEncoderSetParameter(_ctx, BROTLI_PARAM_DISABLE_LITERAL_CONTEXT_MODELING, false),
			"force literal context modeling"
		)
		CHECK_BROTLI_RETURN(
			BrotliEncoderSetParameter(_ctx, BROTLI_PARAM_SIZE_HINT, hint),
			"set size hint to {}", hint
		)
		CHECK_BROTLI_RETURN(
			BrotliEncoderSetParameter(_ctx, BROTLI_PARAM_LARGE_WINDOW, true),
			"enable large window"
		)

		streaming = true;
		out = reinterpret_cast<uint8_t *>(bytes);
		available_out = capacity;
	}

	inline size_t stop()
	{
		if (!streaming)
		[[unlikely]]
			throw std::runtime_error("compression stream not started");

		size_t available_in = 0, total = 0;
		CHECK_BROTLI_RETURN(
			BrotliEncoderCompressStream(
				_ctx, BROTLI_OPERATION_FINISH,
				&available_in, nullptr,
				&available_out, &out,
				&total
			),
			"finalise compression stream"
		)
		BrotliEncoderDestroyInstance(_ctx);

		streaming = false;
		return total;
	}

	inline void operator()(const std::byte * bytes, size_t size)
	{
		if (!streaming)
		[[unlikely]]
			throw std::runtime_error("compression stream should be started before using it");

		auto available_in = size;
		auto in = reinterpret_cast<const uint8_t *>(bytes);
		CHECK_BROTLI_RETURN(
			BrotliEncoderCompressStream(
				_ctx, BROTLI_OPERATION_PROCESS,
				&available_in, &in,
				&available_out, &out,
				nullptr
			),
			"compress data"
		)
		if (available_in > 0)
		[[unlikely]]
			throw std::runtime_error("insufficient output buffer capacity");
	}

	template<std::integral T>
	inline void operator()(const std::vector<T> & content)
	{
		operator()(reinterpret_cast<const std::byte *>(content.data()), content.size() * sizeof(T));
	}
};

class decompressor final
{
	bool streaming;
	const uint8_t * in;
	size_t available_in;
	BrotliDecoderState * _ctx;
public:
	explicit decompressor() : streaming(false) {}

	decompressor(const decompressor &) = delete;
	decompressor(decompressor &&) = delete;

	~decompressor() noexcept
	{
		if (streaming)
			stop();
	}

	decompressor & operator=(const decompressor &) = delete;
	decompressor & operator=(decompressor &&) = delete;

	inline void start(const std::byte * bytes, size_t size)
	{
		if (streaming)
		[[unlikely]]
			throw std::runtime_error("decompression stream already started");

		_ctx = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
		if (_ctx == nullptr)
			throw std::bad_alloc();

		CHECK_BROTLI_RETURN(
			BrotliDecoderSetParameter(_ctx, BROTLI_DECODER_PARAM_LARGE_WINDOW, true),
			"enable large window"
		)

		streaming = true;
		in = reinterpret_cast<const uint8_t *>(bytes);
		available_in = size;
	}

	inline void stop()
	{
		if (!streaming)
		[[unlikely]]
			throw std::runtime_error("decompression stream not started");

		if (available_in > 0 || !BrotliDecoderIsFinished(_ctx))
		[[unlikely]]
			throw std::runtime_error("decompression stream not fully consumed");

		BrotliDecoderDestroyInstance(_ctx);

		streaming = false;
	}

	inline bool operator()(std::byte * bytes, size_t size)
	{
		if (!streaming)
		[[unlikely]]
			throw std::runtime_error("decompression stream should be started before using it");

		auto available_out = size;
		auto out = reinterpret_cast<uint8_t *>(bytes);
		if (auto ret = BrotliDecoderDecompressStream(
			_ctx,
			&available_in, &in,
			&available_out, &out,
			nullptr
		); ret == BROTLI_DECODER_RESULT_ERROR)
		[[unlikely]]
			throw std::runtime_error(fmt::format("failed to decompress data ({})", BrotliDecoderGetErrorCode(_ctx)));
		else
			return ret == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT;
	}

	template<std::integral T>
	inline bool operator()(std::vector<T> & content)
	{
		return operator()(reinterpret_cast<std::byte *>(content.data()), content.size() * sizeof(T));
	}
};

}

template<>
struct is_compressor<brotli::compressor> : std::true_type {};

template<>
struct is_decompressor<brotli::decompressor> : std::true_type {};

}

#endif
