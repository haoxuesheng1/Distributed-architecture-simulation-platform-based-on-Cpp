cmake_minimum_required(VERSION 3.12)
project(TerrainStorageEngineTest)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${PROJECT_SOURCE_DIR})
# 查找线程库（GTest依赖）
find_package(Threads REQUIRED)

# 手动添加GTest源码（适配apt安装的libgtest-dev）
add_subdirectory(/usr/src/gtest ${CMAKE_BINARY_DIR}/gtest)

# 设置可执行程序输出路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

add_subdirectory(terrainStorageEngine_test)