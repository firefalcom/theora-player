#include "theora-player.h"

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <iostream>

int main()
{
    puts( "theora-player-test" );

    SDL_Init( SDL_INIT_VIDEO );
    auto window = SDL_CreateWindow( "theora-player-test", 0, 0, 640, 480, 0 );
    auto renderer = SDL_CreateRenderer( window, -1, 0 );

    SDL_Texture *texture;

    theoraplayer::Player player;

    player.setInitializeCallback(
        [&]( const int width, const int height ) {
            texture = SDL_CreateTexture( renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, width, height );
        } );

    player.setUpdateCallback(
        [&]( const theoraplayer::Player::YCbCrBuffer &yuv, const int width, const int height )
        {
            SDL_UpdateYUVTexture(
                texture, nullptr, yuv[0].data, yuv[0].stride, yuv[1].data, yuv[1].stride, yuv[2].data, yuv[2].stride );
            SDL_RenderClear( renderer );
            SDL_RenderCopy( renderer, texture, nullptr, nullptr );
            SDL_RenderPresent( renderer );

            SDL_Event event;
            while ( SDL_PollEvent( &event ) )
            {
                switch ( event.type )
                {
                    case SDL_QUIT:
                        player.stop();
                        break;
                }
            }
        } );

    player.play( "./res/sample.ogv" );

    SDL_Quit();

    return 0;
}
