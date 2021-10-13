# theora-player

## Description

`theora-player` is an embeddable theora video player C++ library based on the libtheora sample. It has no audio support at this moment.

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

## Usage

```cpp
theoraplayer::Player player;

player.setInitializeCallback(
    [&]( const int width, const int height ) {
        // Implement what you need here.
    } );

player.setUpdateCallback(
    [&]( const theoraplayer::Player::YCbCrBuffer &yuv, const int width, const int height )
    {
       // Use the frame data here.
    } );

player.play( "./res/sample.ogv" );
```

See the `test` directory for a concrete usage.
