# theora-player

## Description

    `theora-player` is a embeddable theora video player written in C++.

## Building the test application

```
cd test
mkdir build
cd build
cmake ..
make
```

## Integration

When using CMake, simply add the following to your `CMakeLists.txt` file:

```
add_subdirectory($(PATH_TO_THEORAPLAYER) theora-player)
target_link_libraries(theora-player-test theora-player)
```
