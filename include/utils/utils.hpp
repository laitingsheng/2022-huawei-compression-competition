#ifndef __UTILS_HPP__
#define __UTILS_HPP__

#include <cstddef>
#include <cstdint>

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <unordered_map>
#include <utility>

#include <fmt/core.h>
#include <fmt/compile.h>

#include "../std.hpp"

namespace utils
{

template<std::path_like Path>
[[using gnu : always_inline]]
inline static void blank_file(Path && file_path, size_t size)
{
	std::ofstream file(std::forward<Path>(file_path), std::ios::binary);
	file.seekp(size - 1).write("", 1);
}

enum class file_type : uint8_t
{
	dat,
	hxv
};

}

#endif
