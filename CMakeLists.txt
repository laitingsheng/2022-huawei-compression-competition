cmake_minimum_required(VERSION 3.16)
project(CompressionCompetition CXX C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(fmt)
include(mio)

include(common)
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -static")

add_executable(main src/main.cpp)
target_include_directories(main PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(main PRIVATE
	fmt::fmt
	mio::mio
)
