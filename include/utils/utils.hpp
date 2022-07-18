#ifndef __UTILS_HPP__
#define __UTILS_HPP__

#include <cstddef>

#include <fstream>
#include <ios>

#include "../std.hpp"

namespace utils
{

[[using gnu : always_inline]]
inline static void blank_file(const std::path_like auto & file_path, size_t size)
{
	std::ofstream file(file_path, std::ios::binary);
	file.seekp(size - 1).write("", 1);
}

}

#endif
