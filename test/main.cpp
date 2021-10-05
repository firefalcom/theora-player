#include "theora-player.h"

#include <SDL2/SDL.h>

SDL_Window *window;
SDL_Surface *surface;

int main()
{
    puts( "theora-player-test" );

    SDL_Init( SDL_INIT_VIDEO );
    window = SDL_CreateWindow( "theora-player-test", 0, 0, 640, 480, 0 );
    surface = SDL_GetWindowSurface( window );

    return 0;
}
