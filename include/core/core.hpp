#ifndef __CORE_HPP__
#define __CORE_HPP__

#include <concepts>
#include <string>

#include <mio/mmap.hpp>

#include "../utils/utils.hpp"
#include "./dat.hpp"
#include "./hxv.hpp"

namespace core
{

template<std::unsigned_integral SizeT>
[[using gnu : always_inline]]
inline static void compress(const std::string & source_path, const std::string & dest_path)
{
	auto source = mio::mmap_source(source_path);
	utils::blank_file(dest_path, source.size());
	auto dest = mio::mmap_sink(dest_path);

	size_t size;
	if (std::string extension = std::filesystem::path(source_path).extension(); extension == ".dat")
		size = dat::compress<SizeT>(source.data(), source.size(), dest.data(), dest.size());
	else if (extension == ".hxv")
		size = hxv::compress<SizeT>(source.data(), source.size(), dest.data(), dest.size());
	else
	[[unlikely]]
		throw std::runtime_error("unexpected file type");

	std::filesystem::resize_file(dest_path, size);
}

inline static void train(const std::string & source_path, const std::string & dest_path)
{
	auto source = mio::mmap_source(source_path);

	if (std::string extension = std::filesystem::path(source_path).extension(); extension == ".dat")
		dat::train_dict<16, 100000>(source.data(), source.size(), dest_path);
	else if (extension == ".hxv")
		hxv::train_dict<16, 100000>(source.data(), source.size(), dest_path);
	else
	[[unlikely]]
		throw std::runtime_error("unexpected file type");
}

template<std::unsigned_integral SizeT>
[[using gnu : always_inline]]
inline static void decompress(const std::string & source_path, const std::string & dest_path)
{
	auto source = mio::mmap_source(source_path);
	auto read_pos = source.data();

	if (auto file_type = static_cast<utils::file_type>(*read_pos++); file_type == utils::file_type::dat)
		dat::decompress<SizeT>(read_pos, source.size() - 1, dest_path);
	else if (file_type == utils::file_type::hxv)
		hxv::decompress<SizeT>(read_pos, source.size() - 1, dest_path);
	else
	[[unlikely]]
		throw std::runtime_error("unexpected file type");
}

}

#endif
