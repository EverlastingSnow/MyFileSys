cmake_minimum_required(VERSION 3.10.0)
project(MyFileSys VERSION 0.1.0 LANGUAGES C)

# 设置可执行文件输出目录为项目根目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(MyFileSys main.c FS.c)

