cmake_minimum_required(VERSION 3.15)
project(imgchecker)

set(CMAKE_CXX_STANDARD 17)

find_package(JPEG REQUIRED)
find_package(TIFF REQUIRED)

add_executable(imgchecker main.cpp)

target_link_libraries(imgchecker JPEG::JPEG TIFF::TIFF)
