#ifndef __UTILS_HPP__
#define __UTILS_HPP__

#include <cstddef>
#include <cstdio>

#include <fstream>
#include <ios>
#include <type_traits>
#include <utility>

#include <fast-lzma2/fast-lzma2.h>
#include <fmt/core.h>
#include <lz4/lz4.h>
#include <lz4/lz4hc.h>

namespace utils
{

template<typename Path>
[[using gnu : always_inline, hot]]
inline void blank_file(Path && file_path, size_t size)
{
	std::ofstream file(std::forward<Path>(file_path), std::ios::binary);
	file.seekp(size - 1);
	file.write("", 1);
}

namespace counter
{

template<typename T, typename CT>
class simple final
{
	T value;
	CT count;
	std::vector<std::pair<T, CT>> counter;
public:
	simple() : value(0), count(0), counter() {}

	void add(T new_value)
	{
		if (value == new_value)
			++count;
		else
		{
			counter.emplace_back(value, count);
			value = new_value;
			count = 1;
		}
	}

	void commit()
	{
		if (count)
			counter.emplace_back(value, count);
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

template<typename T, typename DT, typename CT>
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
		if constexpr (std::is_integral_v<T> && !std::is_same_v<T, DT>)
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

template<typename T, typename CT>
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

namespace lz4
{

template<typename T>
[[nodiscard]]
[[using gnu : always_inline, hot]]
inline char * compress_vector(char * pos, uint32_t & capacity, uint32_t & written, const std::vector<T> & data)
{
	if (capacity < 4)
	{
		fmt::print(stderr, "Not enough space to compress the vector.\n");
		return nullptr;
	}

	int lz4_written = LZ4_compress_HC(
		reinterpret_cast<const char *>(data.data()),
		pos + 4,
		data.size() * sizeof(T),
		capacity - 4,
		LZ4HC_CLEVEL_MAX
	);

	if (lz4_written < 0)
	{
		fmt::print(stderr, "Failed to compress the vector (LZ4HC, {}).\n", lz4_written);
		return nullptr;
	}
	*reinterpret_cast<uint32_t *>(pos) = lz4_written;

	lz4_written += 4;
	written += lz4_written;
	capacity -= lz4_written;
	return pos + lz4_written;
}

template<typename T>
[[nodiscard]]
[[using gnu : always_inline, hot]]
inline const char * decompress_vector(const char * pos, std::vector<T> & data)
{
	uint32_t compressed = *reinterpret_cast<const uint32_t *>(pos), decompressed = data.size() * sizeof(T);
	int lz4_extracted = LZ4_decompress_safe(
		pos + 4,
		reinterpret_cast<char *>(data.data()),
		compressed,
		decompressed
	);

	if (lz4_extracted != decompressed)
	{
		fmt::print(stderr, "Failed to decompress the vector (LZ4HC, {} vs {}).\n", lz4_extracted, decompressed);
		return nullptr;
	}
	return pos + compressed + 4;
}

}

namespace fl2
{

template<typename T>
[[nodiscard]]
[[using gnu : always_inline, hot]]
inline char * compress_vector(char * pos, uint32_t & capacity, uint32_t & written, const std::vector<T> & data)
{
	if (capacity < 4)
	{
		fmt::print(stderr, "Not enough space to compress the vector.\n");
		return nullptr;
	}

	auto ctx = FL2_createCCtx();
	FL2_CCtx_setParameter(ctx, FL2_p_highCompression, 1);
	FL2_CCtx_setParameter(ctx, FL2_p_strategy, 3);
	FL2_CCtx_setParameter(ctx, FL2_p_literalCtxBits, FL2_LC_MAX);
	FL2_CCtx_setParameter(ctx, FL2_p_literalPosBits, FL2_LP_MAX);
	FL2_CCtx_setParameter(ctx, FL2_p_posBits, FL2_PB_MAX);
	FL2_CCtx_setParameter(ctx, FL2_p_compressionLevel, FL2_maxHighCLevel());

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

	FL2_freeCCtx(ctx);

	return pos + fl2_written;
}

template<typename T>
[[nodiscard]]
[[using gnu : always_inline, hot]]
inline const char * decompress_vector(const char * pos, std::vector<T> & data)
{
	uint32_t compressed = *reinterpret_cast<const uint32_t *>(pos), decompressed = data.size() * sizeof(T);
	size_t fl2_extracted = FL2_decompress(data.data(), decompressed, pos + 4, compressed);

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

}

}

#endif
