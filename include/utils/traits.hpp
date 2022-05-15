#ifndef __UTILS_TRAITS_HPP__
#define __UTILS_TRAITS_HPP__

#include <type_traits>

namespace utils
{

template<typename T>
struct is_compressor : std::false_type {};

template<typename T>
inline constexpr bool is_compressor_v = is_compressor<T>::value;

template<typename T>
concept compressor = is_compressor_v<T>;

template<typename T>
struct is_decompressor : std::false_type {};

template<typename T>
inline constexpr bool is_decompressor_v = is_decompressor<T>::value;

template<typename T>
concept decompressor = is_decompressor_v<T>;


}

#endif
