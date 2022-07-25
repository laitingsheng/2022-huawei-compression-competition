#ifndef __CORE_HPP__
#define __CORE_HPP__

#include <cstdint>

#include <filesystem>
#include <string>

#include <mio/mmap.hpp>

#include "./cfd.hpp"

namespace core
{

static void compress(const std::string & source_path, const std::string & dest_path)
{
	cfd<int16_t>::from_file(source_path).compress_to_file(dest_path);
}

static void decompress(const std::string & source_path, const std::string & dest_path)
{
	auto source = mio::mmap_source(source_path);
	auto source_size = source.size();
	utils::blank_file(dest_path, source_size);

	auto sink = mio::mmap_sink(dest_path);
	std::copy(source.begin(), source.end(), sink.begin());
}

}

#endif
