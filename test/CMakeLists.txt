cmake_minimum_required(VERSION 3.14)
project(theora-player-test)

add_subdirectory(.. theora-player)

add_executable(theora-player-test main.cpp)
target_link_libraries(theora-player-test SDL2)
target_link_libraries(theora-player-test theora-player)

