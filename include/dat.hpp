#ifndef __DAT_HPP__
#define __DAT_HPP__

#include <cstdint>

#include <fmt/core.h>

#include "lz4.h"
#include "lz4hc.h"

namespace dat
{

[[nodiscard]]
[[using gnu : always_inline]]
inline int compress(
	const char * begin,
	uint32_t size,
	uint32_t capacity,
	char * output,
	uint32_t & written
)
{
	int lz4_written = LZ4_compress_HC(begin, output, size, capacity, LZ4HC_CLEVEL_MAX);
	if (lz4_written < 0)
		return 1;
	written = lz4_written;
	return 0;
}

[[nodiscard]]
[[using gnu : always_inline]]
inline int decompress(
	const char * begin,
	uint32_t compressed,
	uint32_t decompressed,
	char * output
)
{
	int lz4_extracted = LZ4_decompress_safe(begin, output, compressed, decompressed);
	if (lz4_extracted != decompressed)
		return 1;
	return 0;
}

}

#endif
