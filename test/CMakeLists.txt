cmake_minimum_required(VERSION 3.14)
project(theora-player-test)

add_subdirectory(.. theora-player)

add_executable(theora-player-test main.cpp)

conan_cmake_run(
    REQUIRES
	sdl/2.0.20
    BASIC_SETUP CMAKE_TARGETS
    BUILD missing
    )

target_link_libraries(theora-player-test PUBLIC theora-player CONAN_PKG::sdl)
# target_link_libraries(theora-player-test SDL2)
# target_link_libraries(theora-player-test theora-player)

