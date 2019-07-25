cmake_minimum_required(VERSION 3.14)

project(raft)

# add_definitions(-std=c++11)
set(CMAKE_CXX_STANDARD 17)

file(GLOB SRCS src/*.h src/*.cpp)
add_executable(raft-node ${SRCS})

target_include_directories(raft-node PRIVATE ${CMAKE_SOURCE_DIR}/3rdparty/json/single_include)
target_link_libraries(raft-node pthread)
target_compile_options(raft-node PRIVATE -g)
add_custom_command(TARGET raft-node
    COMMAND cp ${CMAKE_SOURCE_DIR}/start.sh ${CMAKE_BINARY_DIR}/start.sh)