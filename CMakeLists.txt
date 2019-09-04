cmake_minimum_required(VERSION 3.1)
project(videoPlayer)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "-O3 -Wall -Wextra -ggdb")

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})
add_executable(videoPlayer main.cpp VideoPlayer.cpp)

target_link_libraries(videoPlayer swscale avcodec avformat avutil SDL2_ttf ${SDL2_LIBRARIES})