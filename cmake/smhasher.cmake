include(common)

file(GLOB SMHASHER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/wrapped/smhasher/src/*.cpp)

add_library(smhasher STATIC ${SMHASHER_SOURCES})
add_library(smhasher::smhasher ALIAS smhasher)
target_include_directories(smhasher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/wrapped/smhasher/include)
