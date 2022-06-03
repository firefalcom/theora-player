cmake_minimum_required(VERSION 3.14)

project(theora-player)

set(CMAKE_CXX_STANDARD 17)

include(conanbuildinfo.cmake)
conan_basic_setup(KEEP_RPATHS)

add_library(theora-player theora-player.cpp)

