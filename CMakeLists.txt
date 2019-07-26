cmake_minimum_required(VERSION 3.13)

project(raft)

set(CMAKE_CXX_STANDARD 11)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    # Windows 编译选项
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
    add_definitions(-D_UNICODE)
    SET(RAFT_LIBRARIES ws2_32)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    # Linux 平台编译选项
    SET(RAFT_LIBRARIES pthread)
    add_definitions(-g)
endif()

file(GLOB SRCS src/*.h src/*.cpp)
add_executable(raft-node ${SRCS})
target_include_directories(raft-node PRIVATE ${CMAKE_SOURCE_DIR}/3rdparty/json/single_include)
target_link_libraries(raft-node ${RAFT_LIBRARIES})

# 复制脚本到编译输出文件夹下
# add_custom_command(TARGET raft-node
#    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/start.sh ${CMAKE_BINARY_DIR}/start.sh)
