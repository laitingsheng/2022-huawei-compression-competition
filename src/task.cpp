#include <cstdint>
#include <cstdlib>

#include <filesystem>
#include <string>

#include <fmt/core.h>
#include <mio/mmap.hpp>

#include "dat.hpp"
#include "hxv.hpp"
#include "utils.hpp"

[[using gnu : always_inline]]
inline static void compress(std::string source_path, std::string dest_path)
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
		throw std::runtime_error("invalid file format");

	utils::blank_file(dest_path, source.size());
	auto dest = mio::mmap_sink(dest_path);

	auto write_pos = dest.begin();
	*write_pos++ = sep;

	if (is_hxv)
	{
		*write_pos++ = 'h';

		size_t line_count, size;
		hxv::compress(
			source.begin(),
			source.end(),
			sep,
			write_pos + sizeof(size_t),
			source.size() - sizeof(size_t) - 2,
			line_count,
			size
		);

		*reinterpret_cast<size_t *>(write_pos) = line_count;
		std::filesystem::resize_file(dest_path, size + sizeof(size_t) + 2);
	}
	else
	{
		*write_pos++ = 'd';

		size_t line_count, size;
		dat::compress(
			source.begin(),
			source.end(),
			sep,
			write_pos + 2 * sizeof(size_t),
			source.size() - 2 * sizeof(size_t) - 2,
			line_count,
			size
		);

		auto numbers = reinterpret_cast<size_t *>(write_pos);
		numbers[0] = source.size();
		numbers[1] = line_count;

		std::filesystem::resize_file(dest_path, size + 2 * sizeof(size_t) + 2);
	}
}

[[using gnu : always_inline]]
inline static void decompress(std::string source_path, std::string dest_path)
{
	auto source = mio::mmap_source(source_path);
	auto read_pos = source.begin();

	char sep = *read_pos++;
	if (auto format = *read_pos++; format == 'h')
	{
		auto line_count = *reinterpret_cast<const size_t *>(read_pos);

		utils::blank_file(dest_path, line_count * 25);
		auto dest = mio::mmap_sink(dest_path);

		hxv::decompress(read_pos + sizeof(size_t), sep, line_count, dest.data());
	}
	else if (format == 'd')
	{
		auto numbers = reinterpret_cast<const size_t *>(read_pos);
		auto original_size = numbers[0], line_count = numbers[1];

		utils::blank_file(dest_path, original_size);
		auto dest = mio::mmap_sink(dest_path);

		dat::decompress(read_pos + 2 * sizeof(size_t), sep, line_count, dest.data());
	}
	else
		throw std::runtime_error("unsupported compressed file");
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
		compress(argv[2], argv[3]);
	else if (mode == 'd')
		decompress(argv[2], argv[3]);
	else
	{
		fmt::print(stderr, "{}: Invalid arguments supplied.\n", argv[0]);
		return 1;
	}

	return 0;
}
