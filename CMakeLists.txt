cmake_minimum_required(VERSION 3.14)

project(raft)

file(GLOB SRCS src/*.h src/*.cpp)
add_executable(node ${SRCS})

target_include_directories(node PRIVATE ${CMAKE_SOURCE_DIR}/3rdparty/json/single_include)