cmake_minimum_required(VERSION 3.16)
project(CompressionCompetition CXX C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "ON")

include(ctre)
include(fmt)
include(mio)
include(smhasher)

include(common)

add_executable(task src/task.cpp)
target_include_directories(task PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(task PRIVATE
	ctre::ctre
	fmt::fmt
	mio::mio
	smhasher::smhasher
)
