include(common)

add_library(bch STATIC src/linux/bch.c)
add_library(bch::bch ALIAS bch)

target_include_directories(bch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
