include(common)

file(GLOB LZ4_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/lz4/*.c)

add_library(lz4 STATIC ${LZ4_SOURCES})
add_library(lz4::lz4 ALIAS lz4)

target_include_directories(lz4 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/lz4)
