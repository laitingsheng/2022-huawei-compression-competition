#ifndef __CORE_HPP__
#define __CORE_HPP__

#include <concepts>
#include <string>

#include <mio/mmap.hpp>

#include "../utils/fl2.hpp"
#include "../utils/utils.hpp"
#include "./dat.hpp"
#include "./hxv.hpp"

namespace core
{

[[using gnu : always_inline]]
inline static void compress(const std::string & source_path, const std::string & dest_path)
{
	utils::fl2::compressor compressor;
	auto source = mio::mmap_source(source_path);
	if (std::string extension = std::filesystem::path(source_path).extension(); extension == ".dat")
		dat::data::parse(source.data(), source.size()).compress(compressor, dest_path);
	else if (extension == ".hxv")
		hxv::data::parse(source.data(), source.size()).compress(compressor, dest_path);
	else
	[[unlikely]]
		throw std::runtime_error("unexpected file type");
}

[[using gnu : always_inline]]
inline static void decompress(const std::string & source_path, const std::string & dest_path)
{
	auto source = mio::mmap_source(source_path);
	auto read_pos = source.data();

	utils::fl2::decompressor decompressor;
	if (auto file_type = static_cast<utils::file_type>(*read_pos++); file_type == utils::file_type::dat)
		dat::data::decompress(decompressor, read_pos, source.size() - 1).write(dest_path);
	else if (file_type == utils::file_type::hxv)
		hxv::data::decompress(decompressor, read_pos, source.size() - 1).write(dest_path);
	else
	[[unlikely]]
		throw std::runtime_error("unexpected file type");
}

}

#endif
