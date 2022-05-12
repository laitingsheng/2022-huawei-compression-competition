find_package(Threads REQUIRED)

include(common)

file(GLOB FAST_LZMA2_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/fast-lzma2/*.c)

add_library(fl2 STATIC ${FAST_LZMA2_SOURCES})
add_library(fl2::fl2 ALIAS fl2)

target_include_directories(fl2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/fast-lzma2)
target_link_libraries(fl2 PRIVATE Threads::Threads)
