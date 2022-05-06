#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <charconv>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <mio/mmap.hpp>

#include "hxv.hpp"
#include "utils.hpp"

[[nodiscard]]
[[using gnu : always_inline]]
inline static int compress_dat(char sep, const char * begin, const char * end, char * output, uint32_t & line_count)
{
	line_count = 0;
	auto write_pos = output;
	for (auto pos = begin; pos < end;)
	{
		auto cells = reinterpret_cast<double *>(write_pos);
		write_pos += 568;

		for (size_t i = 0; i < 71; ++i)
		{
			char * ptr;
			double cell = strtod(pos, &ptr);
			if (ptr == pos)
			{
				fmt::print(stderr, "Unexpected character (ASCII {}).\n", size_t(*ptr));
				return 2;
			}

			if (char c = *ptr; c == sep || c == '\n')
				pos = ptr + 1;
			else if (c == '\r')
				pos = ptr + 2;
			else
			{
				fmt::print(stderr, "Unexpected character (ASCII {}).\n", size_t(c));
				return 2;
			}

			cells[i] = cell;
		}

		++line_count;
	}
	return 0;
}

[[nodiscard]]
[[using gnu : always_inline]]
inline static int compress(std::string source_path, std::string dest_path)
{
	auto source = mio::mmap_source(source_path);

	std::string buffer;
	buffer.reserve(100);
	char sep = 0;
	bool is_hxv = true;
	for (char c : source)
	{
		if (c == ',' || c == ' ')
		{
			sep = c;
			break;
		}
		else if (!isdigit(c))
			is_hxv = false;
		buffer.push_back(c);
	}

	if (!sep)
	{
		fmt::print(stderr, "Invalid file format.\n");
		return 1;
	}

	utils::blank_file(dest_path, source.size());
	auto dest = mio::mmap_sink(dest_path);

	auto write_pos = dest.begin();
	*write_pos++ = sep;

	if (is_hxv)
	{
		*write_pos++ = 'h';

		uint32_t line_count, size;
		if (auto ret = hxv::compress(
			source.begin(),
			source.end(),
			sep,
			line_count,
			write_pos + 4,
			source.size() - 6,
			size
		); ret)
		{
			fmt::print(stderr, "Error while compressing HXV ({}).\n", ret);
			return ret;
		}

		*reinterpret_cast<uint32_t *>(write_pos) = line_count;
		std::filesystem::resize_file(dest_path, size + 6);
	}
	else
	{
		uint32_t line_count, line_width, extra_size;
		*write_pos++ = 'd';
		if (auto ret = compress_dat(sep, source.begin(), source.end(), write_pos + 8, line_count); ret)
		{
			fmt::print(stderr, "Error while compressing DAT ({}).\n", ret);
			return ret;
		}

		auto numbers = reinterpret_cast<uint32_t *>(write_pos);
		numbers[0] = line_count;
		numbers[1] = source.size();

		line_width = 568;
		extra_size = 10;

		std::filesystem::resize_file(dest_path, line_count * line_width + extra_size);
	}

	return 0;
}

inline static int decompress_dat(char sep, uint32_t line_count, const char * begin, const char * end, char * output)
{
	auto read_pos = begin;
	auto write_pos = output;
	for (uint32_t line = 0; line < line_count; ++line)
	{
		auto cells = reinterpret_cast<double const *>(read_pos);
		read_pos += 568;

		auto cell = cells[0];
		if (auto ptr = fmt::format_to(write_pos, "{:.3f}", cell); ptr == write_pos)
		{
			fmt::print(stderr, "Error while extracting number.\n");
			return 2;
		}
		else
			write_pos = ptr;
		*write_pos++ = sep;

		for (size_t i = 1; i < 71; ++i)
		{
			cell = cells[i];
			if (auto ptr = fmt::format_to(write_pos, "{:.5f}", cell); ptr == write_pos)
			{
				fmt::print(stderr, "Error while extracting number.\n");
				return 2;
			}
			else
				write_pos = ptr;
			if (i == 70)
			{
				*write_pos++ = '\r';
				*write_pos++ = '\n';
			}
			else
				*write_pos++ = sep;
		}
	}
	return 0;
}

inline static int decompress(std::string source_path, std::string dest_path)
{
	auto source = mio::mmap_source(source_path);
	auto read_pos = source.begin();

	char sep = *read_pos++;
	if (auto format = *read_pos++; format == 'h')
	{
		auto line_count = *reinterpret_cast<uint32_t const *>(read_pos);

		utils::blank_file(dest_path, line_count * 25);
		auto dest = mio::mmap_sink(dest_path);

		if (auto ret = hxv::decompress(read_pos + 4, sep, line_count, dest.data()); ret)
		{
			fmt::print(stderr, "Error while decompressing HXV ({}).\n", ret);
			return ret;
		}
	}
	else if (format == 'd')
	{
		auto numbers = reinterpret_cast<uint32_t const *>(read_pos);
		auto line_count = numbers[0], decompressed_size = numbers[1];

		utils::blank_file(dest_path, decompressed_size);
		auto dest = mio::mmap_sink(dest_path);

		if (auto ret = decompress_dat(sep, line_count, read_pos + 8, source.end(), dest.begin()); ret)
		{
			fmt::print(stderr, "Error while decompressing DAT ({}).\n", ret);
			return ret;
		}
	}
	else
	{
		fmt::print(stderr, "Unsupported compressed file.\n");
		return 1;
	}

	return 0;
}

int main(int argc, char * argv[])
{
	if (argc < 4)
	{
		fmt::print(stderr, "{}: Expect at least 3 arguments (got {}).\n", argv[0], argc - 1);
		return 1;
	}

	char mode = argv[1][0];
	if (mode == 'c')
		return compress(argv[2], argv[3]);
	if (mode == 'd')
		return decompress(argv[2], argv[3]);
	fmt::print(stderr, "{}: Invalid arguments supplied.\n", argv[0]);
	return 1;

	return 0;
}
