cmake_minimum_required(VERSION 3.14)

add_library(theora-player src/main.cpp)
target_include_directories(theora-player PUBLIC ./include)

