#ifndef __STD_COMPLEMENTS_HPP__
#define __STD_COMPLEMENTS_HPP__

#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>

namespace std
{

template<typename T>
concept regular_type = is_object_v<T>;

template<typename T>
concept reference_type = is_reference_v<T>;

template<typename T>
concept const_type = is_const_v<T>;

template<typename T>
concept volatile_type = is_volatile_v<T>;

template<typename T>
struct is_char_type : false_type {};

template<>
struct is_char_type<char> : true_type {};

template<>
struct is_char_type<wchar_t> : true_type {};

template<>
struct is_char_type<char8_t> : true_type {};

template<>
struct is_char_type<char16_t> : true_type {};

template<>
struct is_char_type<char32_t> : true_type {};

template<typename T>
inline constexpr bool is_char_type_v = is_char_type<T>::value;

template<typename T>
concept char_type = is_char_type_v<T>;

template<typename T>
struct is_string_like : false_type {};

template<reference_type T>
struct is_string_like<T> : is_string_like<remove_reference_t<T>> {};

template<const_type T>
struct is_string_like<T> : is_string_like<remove_const_t<T>> {};

template<volatile_type T>
struct is_string_like<T> : is_string_like<remove_volatile_t<T>> {};

template<char_type C>
struct is_string_like<const C *> : true_type {};

template<char_type C>
struct is_string_like<C *> : true_type {};

template<char_type CharT, typename Traits, typename Allocator>
struct is_string_like<basic_string<CharT, Traits, Allocator>> : true_type {};

template<char_type CharT, typename Traits>
struct is_string_like<basic_string_view<CharT, Traits>> : true_type {};

template<typename T>
inline constexpr bool is_string_like_v = is_string_like<T>::value;

template<typename T>
concept string_like = is_string_like_v<T>;

template<typename T>
struct is_path_like : false_type {};

template<string_like T>
struct is_path_like<T> : true_type {};

template<>
struct is_path_like<filesystem::path> : true_type {};

template<typename T>
inline constexpr bool is_path_like_v = is_path_like<T>::value;

template<typename T>
concept path_like = is_path_like_v<T>;

}

#endif
