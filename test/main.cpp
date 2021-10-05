#include "theora-player.h"

#include <SDL2/SDL.h>

/* SDL_Window *window; */
/* SDL_Surface *surface; */

int main()
{
    puts( "theora-player-test" );

    theoraplayer::Player player;

    player.play("./res/sample.ogv");

    /* SDL_Init( SDL_INIT_VIDEO ); */
    /* window = SDL_CreateWindow( "theora-player-test", 0, 0, 640, 480, 0 ); */
    /* surface = SDL_GetWindowSurface( window ); */

    /* SDL_Delay( 2000 ); */

    /* SDL_Quit(); */

    return 0;
}
