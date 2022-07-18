#ifndef __CORE_HPP__
#define __CORE_HPP__

#include <cstddef>

#include <filesystem>
#include <stdexcept>
#include <string>

#include "./utils/fl2.hpp"
#include "./utils/utils.hpp"

namespace core
{

static void compress(const std::string & source_path, const std::string & dest_path)
{
	auto source = mio::mmap_source(source_path);
	auto source_size = source.size();
	utils::blank_file(dest_path, source_size);

	auto sink = mio::mmap_sink(dest_path);
	*reinterpret_cast<size_t *>(sink.data()) = source_size;
	utils::fl2::compressor compressor;
	compressor.start(reinterpret_cast<std::byte *>(sink.data() + sizeof(size_t)), source_size - sizeof(size_t));
	compressor(source.data(), source_size);

	std::filesystem::resize_file(dest_path, sizeof(size_t) + compressor.stop());
}

static void decompress(const std::string & source_path, const std::string & dest_path)
{
	auto source = mio::mmap_source(source_path);
	auto source_size = source.size();
	if (source_size < 8)
	[[unlikely]]
		throw std::runtime_error("invalid file");
	auto original_size = *reinterpret_cast<const size_t *>(source.data());
	utils::blank_file(dest_path, original_size);

	auto sink = mio::mmap_sink(dest_path);
	utils::fl2::decompressor decompressor;
	decompressor.start(reinterpret_cast<const std::byte *>(source.data() + sizeof(size_t)), source_size - sizeof(size_t));
	if (decompressor(sink.data(), original_size))
		throw std::runtime_error("redundant data found at the end of the file");
}

}

#endif
