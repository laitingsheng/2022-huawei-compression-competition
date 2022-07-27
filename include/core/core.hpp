#ifndef __CORE_HPP__
#define __CORE_HPP__

#include <string>

#include "./cfd.hpp"

namespace core
{

static void compress(const std::string & source_path, const std::string & dest_path)
{
	cfd::from_file(source_path).compress_to_file(dest_path);
}

static void decompress(const std::string & source_path, const std::string & dest_path)
{
	cfd::from_compressed(source_path).reconstruct(dest_path);
}

}

#endif
