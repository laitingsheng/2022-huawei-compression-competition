#include <cstdint>
#include <cstdlib>

#include <filesystem>
#include <string>
#include <string_view>

#include <fmt/core.h>
#include <fmt/compile.h>
#include <mio/mmap.hpp>

#include "dict/dat.hpp"
#include "dict/hxv.hpp"
#include "utils.hpp"

int main(const int argc, const char * const argv[])
{
	if (argc < 4)
	{
		fmt::print(stderr, FMT_STRING("{}: Expect at least 3 arguments (got {}).\n"), argv[0], argc - 1);
		return 1;
	}

	const char mode = argv[1][0];
	const auto source_path = argv[2], dest_path = argv[3];

	if (mode == 'c')
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
		[[unlikely]]
		{
			fmt::print(stderr, FMT_STRING("{}: Separator not found.\n"), argv[0]);
			return 1;
		}

		utils::blank_file(dest_path, source.size());
		auto dest = mio::mmap_sink(dest_path);

		auto write_pos = dest.begin();
		*write_pos++ = sep;

		size_t line_count = 0, size = 2, capacity = source.size() - 2;

		if (is_hxv)
		{
			*write_pos++ = 'h';

			utils::counter::differential<uint8_t, size_t> counter_column1;
			std::array<std::vector<uint8_t>, 9> standard_columns;

			for (auto pos = source.begin(); pos < source.end();)
			[[likely]]
			{
				standard_columns[0].push_back(hxv::from_hex_chars(pos));
				pos += 2;

				counter_column1.add(hxv::from_hex_chars(pos));
				pos += 2;

				if (char c = *pos++; c != sep)
				[[unlikely]]
				{
					fmt::print(
						stderr,
						FMT_STRING("{}: Unexpected character (ASCII 0x{:02x}), expect ASCII 0x{:02x}.\n"),
						argv[0],
						c,
						sep
					);
					return 1;
				}

				for (size_t i = 2; i < 10; i += 2)
				[[likely]]
				{
					standard_columns[i - 1].push_back(hxv::from_hex_chars(pos));
					pos += 2;

					standard_columns[i].push_back(hxv::from_hex_chars(pos));
					pos += 2;

					char expected;
					if (i == 8)
					[[unlikely]]
						expected = '\n';
					else
					[[likely]]
						expected = sep;

					if (char c = *pos++; c != expected)
					[[unlikely]]
					{
						fmt::print(
							stderr,
							FMT_STRING("{}: Unexpected character (ASCII 0x{:02x}), expect ASCII 0x{:02x}.\n"),
							argv[0],
							c,
							expected
						);
						return 1;
					}
				}

				++line_count;
			}
			counter_column1.commit();

			write_pos += sizeof(size_t);
			size += sizeof(size_t);
			capacity -= sizeof(size_t);

			auto written = utils::counter::write(write_pos, capacity, counter_column1.data());
			write_pos += written;
			size += written;
			capacity -= written;

			utils::fl2::compressor<hxv::DICT> compressor;

			for (const auto & column : standard_columns)
			{
				written = compressor(write_pos, capacity, column);
				write_pos += written;
				size += written;
				capacity -= written;
			}

			*reinterpret_cast<size_t *>(dest.data() + 2) = line_count;
		}
		else
		{
			*write_pos++ = 'd';

			utils::counter::differential<int64_t, size_t> counter_column0;
			std::array<std::vector<int64_t>, 70> standard_columns;

			for (auto pos = source.begin(); pos < source.end();)
			[[likely]]
			{
				auto [output, offset] = dat::float_to_integer<3, int64_t>(pos);
				counter_column0.add(output);
				pos += offset;

				if (char c = *pos++; c != sep)
				[[unlikely]]
				{
					fmt::print(
						stderr,
						FMT_STRING("{}: Unexpected character (ASCII 0x{:02x}), expect ASCII 0x{:02x}.\n"),
						argv[0],
						c,
						sep
					);
					return 1;
				}

				for (size_t i = 1; i < 71; ++i)
				{
					auto [output, offset] = dat::float_to_integer<5, int64_t>(pos);
					standard_columns[i - 1].push_back(output);
					pos += offset;

					char expected;
					if (i == 70)
					[[unlikely]]
						expected = '\r';
					else
					[[likely]]
						expected = sep;

					if (char c = *pos++; c != expected)
					[[unlikely]]
					{
						fmt::print(
							stderr,
							FMT_STRING("{}: Unexpected character (ASCII 0x{:02x}), expect ASCII 0x{:02x}.\n"),
							argv[0],
							c,
							expected
						);
						return 1;
					}
				}

				if (char c = *pos++; c != '\n')
				[[unlikely]]
				{
					fmt::print(
						stderr,
						FMT_STRING("{}: Unexpected character (ASCII 0x{:02x}), expect ASCII 0x0a.\n"),
						argv[0],
						c
					);
					return 1;
				}

				++line_count;
			}
			counter_column0.commit();

			write_pos += 2 * sizeof(size_t);
			size += 2 * sizeof(size_t);
			capacity -= 2 * sizeof(size_t);

			auto written = utils::counter::write(write_pos, capacity, counter_column0.data());
			write_pos += written;
			size += written;
			capacity -= written;

			utils::fl2::compressor<dat::DICT> compressor;

			for (const auto & column : standard_columns)
			{
				written = compressor(write_pos, capacity, column);
				write_pos += written;
				size += written;
				capacity -= written;
			}

			auto numbers = reinterpret_cast<size_t *>(dest.data() + 2);
			numbers[0] = source.size();
			numbers[1] = line_count;
		}

		std::filesystem::resize_file(dest_path, size);
	}
	else if (mode == 'd')
	{
		auto source = mio::mmap_source(source_path);
		auto read_pos = source.begin();

		char sep = *read_pos++;
		if (auto format = *read_pos++; format == 'h')
		{
			const auto line_count = *reinterpret_cast<const size_t *>(read_pos);
			read_pos += sizeof(size_t);

			utils::blank_file(dest_path, line_count * 25);
			auto dest = mio::mmap_sink(dest_path);

			std::vector<uint8_t> buffer(line_count);

			auto read = utils::counter::reconstruct_differential<uint8_t, size_t>(read_pos, buffer);
			read_pos += read;
			hxv::write_vector(buffer, dest.data() + 2);

			utils::fl2::decompressor<hxv::DICT> decompressor;

			read = decompressor(read_pos, buffer);
			read_pos += read;
			hxv::write_vector(buffer, dest.data());

			hxv::write_separator(sep, line_count, dest.data() + 4);

			for (size_t offset = 5; offset < 25; offset += 5)
			[[likely]]
			{
				read = decompressor(read_pos, buffer);
				read_pos += read;
				hxv::write_vector(buffer, dest.data() + offset);

				read = decompressor(read_pos, buffer);
				read_pos += read;
				hxv::write_vector(buffer, dest.data() + offset + 2);

				hxv::write_separator(offset == 20 ? '\n' : sep, line_count, dest.data() + offset + 4);
			}
		}
		else if (format == 'd')
		{
			auto numbers = reinterpret_cast<const size_t *>(read_pos);
			const auto original_size = numbers[0], line_count = numbers[1];
			read_pos += 2 * sizeof(size_t);

			utils::blank_file(dest_path, original_size);
			auto dest = mio::mmap_sink(dest_path);

			std::array<std::vector<int64_t>, 71> columns;
			for (auto & column : columns)
				column.resize(line_count);

			auto read = utils::counter::reconstruct_differential<int64_t, size_t>(read_pos, columns[0]);
			read_pos += read;

			utils::fl2::decompressor<dat::DICT> decompressor;

			for (size_t i = 1; i < 71; ++i)
			[[likely]]
			{
				read = decompressor(read_pos, columns[i]);
				read_pos += read;
			}

			auto write_pos = dest.data();
			for (size_t i = 0; i < line_count; ++i)
			[[likely]]
			{
				auto written = dat::write_integer_as_float<3, int64_t>(columns[0][i], write_pos);
				write_pos += written;
				*write_pos++ = sep;

				for (size_t j = 1; j < 71; ++j)
				[[likely]]
				{
					written = dat::write_integer_as_float<5, int64_t>(columns[j][i], write_pos);
					write_pos += written;
					*write_pos++ = j == 70 ? '\r' : sep;
				}

				*write_pos++ = '\n';
			}
		}
		else
		[[unlikely]]
		{
			fmt::print(stderr, FMT_STRING("{}: Compressed file corrupted (invalid format {})\n"), argv[0], format);
			return 1;
		}

	}
	// else if (mode == 't')
	// 	train(argv[2], argv[3]);
	else
	[[unlikely]]
	{
		fmt::print(stderr, FMT_STRING("{}: Invalid arguments supplied.\n"), argv[0]);
		return 1;
	}

	return 0;
}
