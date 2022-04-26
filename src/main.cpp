#include <cstdint>
#include <cstdlib>

#include <charconv>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <fmt/core.h>
#include <fmt/format.h>
#include <mio/mmap.hpp>

inline static void blank_file(const std::string & file_path, size_t size)
{
	std::ofstream file(file_path, std::ios::binary);
	file.seekp(size - 1);
	file.write("", 1);
}

inline static int compress_hxv(char sep, const char * begin, const char * end, char * output, uint64_t & line_count)
{
	auto write_pos = output;
	for (auto pos = begin; pos < end;)
	{
		auto cells = reinterpret_cast<uint16_t *>(write_pos);
		write_pos += 10;

		for (size_t i = 0; i < 5; ++i)
		{
			uint16_t cell = 0;
			auto ptr = pos;
			for(; ptr < pos + 4; ++ptr)
			{
				char c = *ptr;
				uint16_t digit = c >= 'A' ? c - 'A' + 10 : c - '0';
				cell = (cell << 4) + digit;
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

inline static int compress_dat(char sep, const char * begin, const char * end, char * output, uint64_t & line_count)
{
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

inline static int compress(std::string source_path, std::string dest_path)
{
	auto source = mio::mmap_source(source_path);

	std::string buffer;
	buffer.reserve(100);
	char sep = 0;
	for (char c : source)
	{
		if (c == ',' || c == ' ')
		{
			sep = c;
			break;
		}
		buffer.push_back(c);
	}

	if (!sep)
	{
		fmt::print(stderr, "Invalid file format.\n");
		return 1;
	}

	bool is_hxv = true;
	for (char c : buffer)
		if (!isdigit(c))
		{
			is_hxv = false;
			break;
		}

	blank_file(dest_path, source.size());
	auto dest = mio::mmap_sink(dest_path);


	auto write_pos = dest.begin();
	*write_pos++ = sep;

	uint64_t line_count = 0;

	if (is_hxv)
	{
		*write_pos++ = 'h';
		if (auto ret = compress_hxv(sep, source.begin(), source.end(), write_pos + 8, line_count); ret)
		{
			fmt::print(stderr, "Error while compressing HXV ({}).\n", ret);
			return ret;
		}

		*reinterpret_cast<uint64_t *>(write_pos) = line_count;
		std::filesystem::resize_file(dest_path, line_count * 10 + 10);
	}
	else
	{
		*write_pos++ = 'd';
		if (auto ret = compress_dat(sep, source.begin(), source.end(), write_pos + 16, line_count); ret)
		{
			fmt::print(stderr, "Error while compressing DAT ({}).\n", ret);
			return ret;
		}

		auto numbers = reinterpret_cast<uint64_t *>(write_pos);
		numbers[0] = line_count;
		numbers[1] = source.size();
		std::filesystem::resize_file(dest_path, line_count * 568 + 18);
	}

	return 0;
}

int decompress_hxv(char sep, uint64_t line_count, const char * begin, const char * end, char * output)
{
	auto read_pos = begin;
	auto write_pos = output;
	for (uint64_t line = 0; line < line_count; ++line)
	{
		auto cells = reinterpret_cast<uint16_t const *>(read_pos);
		read_pos += 10;

		for(size_t i = 0; i < 5; ++i)
		{
			auto cell = cells[i];
			for (auto reverse = write_pos + 3; reverse >= write_pos; --reverse)
			{
				auto digit = cell & 0b1111;
				char c = digit < 10 ? '0' + digit : 'A' + digit - 10;
				*reverse = c;
				cell >>= 4;
			}

			write_pos += 4;
			*write_pos++ = i == 4 ? '\n' : sep;
		}
	}
	return 0;
}

int decompress_dat(char sep, uint64_t line_count, const char * begin, const char * end, char * output)
{
	auto read_pos = begin;
	auto write_pos = output;
	for (uint64_t line = 0; line < line_count; ++line)
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
		auto line_count = *reinterpret_cast<uint64_t const *>(read_pos);

		blank_file(dest_path, line_count * 25);
		auto dest = mio::mmap_sink(dest_path);

		if (auto ret = decompress_hxv(sep, line_count, read_pos + 8, source.end(), dest.begin()); ret)
		{
			fmt::print(stderr, "Error while decompressing HXV ({}).\n", ret);
			return ret;
		}
	}
	else if (format == 'd')
	{
		auto numbers = reinterpret_cast<uint64_t const *>(read_pos);
		auto line_count = numbers[0], decompressed_size = numbers[1];

		blank_file(dest_path, decompressed_size);
		auto dest = mio::mmap_sink(dest_path);

		if (auto ret = decompress_dat(sep, line_count, read_pos + 16, source.end(), dest.begin()); ret)
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
