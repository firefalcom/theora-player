cmake_minimum_required(VERSION 3.14)

add_library(theora-player src/theora-player.cpp)

target_include_directories(theora-player PUBLIC ./include)

target_link_libraries(theora-player PUBLIC theora ogg theoradec)

