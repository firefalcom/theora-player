cmake_minimum_required(VERSION 3.14)

if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
  message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
  file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/v0.16.1/conan.cmake"
                "${CMAKE_BINARY_DIR}/conan.cmake"
                EXPECTED_HASH SHA256=396e16d0f5eabdc6a14afddbcfff62a54a7ee75c6da23f32f7a31bc85db23484
                TLS_VERIFY ON)
endif()

include(${CMAKE_BINARY_DIR}/conan.cmake)

conan_add_remote(NAME firefalcom
                 INDEX 0
                 URL http://ivy.firefal.com:9300
                 VERIFY_SSL True)

conan_add_remote(NAME conan-center
                 INDEX 1
                 URL https://center.conan.io
                 VERIFY_SSL True)

add_library(theora-player src/theora-player.cpp)

conan_cmake_run(
    REQUIRES
        theora/1.1.1
    BASIC_SETUP CMAKE_TARGETS
    BUILD missing
    )

target_include_directories(theora-player PUBLIC ./include)

target_link_libraries(theora-player PUBLIC CONAN_PKG::theora theora-player-test)

if(WIN32)
target_link_libraries( theora-player PUBLIC Ws2_32.lib )
endif()

