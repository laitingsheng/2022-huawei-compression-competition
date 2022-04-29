#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <charconv>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <mio/mmap.hpp>

inline static void blank_file(const std::string & file_path, size_t size)
{
	std::ofstream file(file_path, std::ios::binary);
	file.seekp(size - 1);
	file.write("", 1);
}

template<typename T>
inline static T from_hex_chars(const char * input)
{
	T re = 0;
	for (auto ptr = input; ptr < input + 2 * sizeof(T); ++ptr)
	{
		char c = *ptr;
		re = (re << 4) + (c >= 'A' ? c - 'A' + 10 : c - '0');
	}
	return re;
}

inline static const char * hxv_read_line(uint8_t & ss, uint8_t & nn, uint16_t * output, const char * pos, char sep)
{
	ss = from_hex_chars<uint8_t>(pos);
	pos += 2;
	nn = from_hex_chars<uint8_t>(pos);
	pos += 3;

	for (size_t i = 0; i < 4; ++i)
	{
		uint16_t cell = 0;
		output[i] = from_hex_chars<uint16_t>(pos);
		pos += 4;

		if (char c = *pos; c == sep || c == '\n')
			++pos;
		else
		{
			fmt::print(stderr, "Unexpected character (ASCII {}).\n", size_t(c));
			return nullptr;
		}
	}
	return pos;
}

template<typename T, typename DT, typename CT>
class entropy_counter final
{
	T value;
	DT diff;
	CT count;
	std::vector<std::pair<DT, CT>> counter;
public:
	entropy_counter() : value(0), diff(0), count(0), counter() {}

	void add(T new_value)
	{
		DT current_diff = DT(new_value) - DT(value);
		if (current_diff == diff)
			++count;
		else
		{
			if (count)
				counter.emplace_back(diff, count);
			diff = current_diff;
			count = 1;
		}
		value = new_value;
	}

	auto & commit()
	{
		if (count)
			counter.emplace_back(diff, count);
		return *this;
	}

	auto size() const
	{
		return counter.size();
	}

	char * write_to(char * pos) const
	{
		*reinterpret_cast<uint64_t *>(pos) = uint64_t(counter.size());
		pos += 8;
		for (const auto & [diff, count] : counter)
		{
			*reinterpret_cast<DT *>(pos) = diff;
			pos += sizeof(DT);
			*reinterpret_cast<CT *>(pos) = count;
			pos += sizeof(CT);
		}
		return pos;
	}
};

inline static int compress_hxv(char sep, const char * begin, const char * end, char * output, uint64_t & line_count, uint64_t & ss_size, uint64_t & nn_size)
{
	entropy_counter<uint8_t, int16_t, uint64_t> ss_counter, nn_counter;

	line_count = 0;
	auto write_pos = output;
	for (auto pos = begin; pos < end;)
	{
		uint8_t ss, nn;
		uint16_t buffer[4];
		if (auto ptr = hxv_read_line(ss, nn, buffer, pos, sep); ptr)
		{
			pos = ptr;
			++line_count;
		}
		else
			return 1;

		ss_counter.add(ss);
		nn_counter.add(nn);
		std::copy(buffer, buffer + 4, reinterpret_cast<uint16_t *>(write_pos));
		write_pos += 8;
	}

	write_pos = ss_counter.commit().write_to(write_pos);
	ss_size = ss_counter.size();

	nn_counter.commit().write_to(write_pos);
	nn_size = nn_counter.size();

	return 0;
}

inline static int compress_dat(char sep, const char * begin, const char * end, char * output, uint64_t & line_count)
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

	uint64_t line_count;
	size_t line_width, extra_size;
	if (is_hxv)
	{
		*write_pos++ = 'h';
		uint64_t ss_size, nn_size;
		if (auto ret = compress_hxv(sep, source.begin(), source.end(), write_pos + 8, line_count, ss_size, nn_size); ret)
		{
			fmt::print(stderr, "Error while compressing HXV ({}).\n", ret);
			return ret;
		}

		*reinterpret_cast<uint64_t *>(write_pos) = line_count;

		line_width = 8;
		extra_size = 10 + 16 + (ss_size + nn_size) * 10;
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

		line_width = 568;
		extra_size = 18;
	}

	std::filesystem::resize_file(dest_path, line_count * line_width + extra_size);

	return 0;
}

template<typename T>
void to_hex_chars(T value, char * output)
{
	for (auto reverse = output + 2 * sizeof(T) - 1; reverse >= output; --reverse)
	{
		uint8_t digit = value & 0xF;
		*reverse = digit < 10 ? '0' + digit : 'A' + digit - 10;
		value >>= 4;
	}
}

const char * reconstruct_hxv_value_from_entropy(const char * read_pos, char * write_pos)
{
	uint64_t size = *reinterpret_cast<const uint64_t *>(read_pos);
	read_pos += 8;

	uint8_t value = 0;
	for (uint64_t i = 0; i < size; ++i)
	{
		int16_t diff = *reinterpret_cast<const int16_t *>(read_pos);
		read_pos += 2;
		uint64_t count = *reinterpret_cast<const uint64_t *>(read_pos);
		read_pos += 8;

		for (size_t j = 0; j < count; ++j)
		{
			value += diff;
			to_hex_chars(value, write_pos);
			write_pos += 25;
		}
	}

	return read_pos;
}

inline static int decompress_hxv(char sep, uint64_t line_count, const char * begin, const char * end, char * output)
{
	auto read_pos = begin;
	auto write_pos = output;
	for (uint64_t line = 0; line < line_count; ++line)
	{
		write_pos += 4;
		*write_pos++ = sep;

		auto cells = reinterpret_cast<uint16_t const *>(read_pos);
		read_pos += 8;

		for(size_t i = 0; i < 4; ++i)
		{
			to_hex_chars(cells[i], write_pos);
			write_pos += 4;
			*write_pos++ = i == 3 ? '\n' : sep;
		}
	}
	read_pos = reconstruct_hxv_value_from_entropy(read_pos, output);
	reconstruct_hxv_value_from_entropy(read_pos, output + 2);
	return 0;
}

inline static int decompress_dat(char sep, uint64_t line_count, const char * begin, const char * end, char * output)
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
