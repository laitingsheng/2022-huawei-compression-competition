cmake_minimum_required(VERSION 3.16)
project(CompressionCompetition CXX C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "ON")

include(fmt)
include(mio)
# include(lz4)
# include(fast-lzma2)
include(zstd)

include(common)

add_executable(task src/task.cpp)
target_include_directories(task PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/external/zstd/lib
)
target_link_libraries(task PRIVATE
	fmt::fmt
	mio::mio
	# lz4::lz4
	# fl2::fl2
	libzstd_static
)
