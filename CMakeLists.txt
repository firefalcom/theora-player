cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_STANDARD 17)

if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
  message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
  file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/v0.16.1/conan.cmake"
                "${CMAKE_BINARY_DIR}/conan.cmake"
                EXPECTED_HASH SHA256=396e16d0f5eabdc6a14afddbcfff62a54a7ee75c6da23f32f7a31bc85db23484
                TLS_VERIFY ON)
endif()

include(${CMAKE_BINARY_DIR}/conan.cmake)

conan_add_remote(NAME conan-center
                 INDEX 1
                 URL https://center.conan.io
                 VERIFY_SSL True)

add_library(theora-player src/theora-player.cpp)

if(NOT THEORA_PLAYER_SKIP_CONAN)
    conan_cmake_run(
        REQUIRES
            ogg/1.3.5
            theora/1.1.1
            vorbis/1.3.7
        BASIC_SETUP CMAKE_TARGETS
        BUILD missing
        )
endif()

if(TARGET CONAN_PKG::theora)
    target_link_libraries(theora-player PUBLIC CONAN_PKG::theora)
endif()

if(TARGET CONAN_PKG::vorbis)
    target_link_libraries(theora-player PUBLIC CONAN_PKG::vorbis)
endif()

target_include_directories(theora-player PUBLIC ./include)

if(WIN32)
    target_link_libraries(theora-player PUBLIC Ws2_32.lib)
endif()
