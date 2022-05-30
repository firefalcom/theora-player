#include "theora-player.h"

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <iostream>
#include <vector>
#include <queue>

theoraplayer::AudioPacketQueue *audioPacketQueue{ nullptr };
SDL_AudioDeviceID audioDeviceId;

static void SDLCALL audioCallback( void *user_data, Uint8 *stream, int len )
{
    if ( !audioPacketQueue || audioPacketQueue->empty() )
    {
        puts( "no data yet" );
        return;
    }

    const auto &packet = audioPacketQueue->front();
    audioPacketQueue->pop();

    memcpy( stream, packet.data, packet.size );
}

int main( int argc, char *argv[] )
{
    puts( "theora-player-test" );

    SDL_Init( SDL_INIT_VIDEO | SDL_INIT_AUDIO );

    auto window = SDL_CreateWindow( "theora-player-test", 0, 0, 640, 480, 0 );
    auto renderer = SDL_CreateRenderer( window, -1, 0 );

    SDL_Texture *texture;

    theoraplayer::Player player;

    player.setInitializeCallback(
        [&]( const int width, const int height, const theoraplayer::AudioSettings &audio_settings )
        {
            texture = SDL_CreateTexture( renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, width, height );

            SDL_AudioSpec audio_spec{};
            audio_spec.freq = audio_settings.frequency;
            audio_spec.format = AUDIO_S16LSB;
            audio_spec.channels = audio_settings.channels;
            audio_spec.samples = audio_settings.samples;
            audio_spec.callback = audioCallback;

            audioDeviceId = SDL_OpenAudioDevice( nullptr, 0, &audio_spec, &audio_spec, 0 );

            if ( audioDeviceId < 0 )
            {
                puts( SDL_GetError() );
            }
        } );

    player.setUpdateCallback(
        [&]( const theoraplayer::YCbCrBuffer &yuv, const int width, const int height )
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
                        audioPacketQueue = nullptr;
                        player.stop();
                        break;
                }
            }
        } );

    player.setAudioUpdateCallback(
        [&]( theoraplayer::AudioPacketQueue &audio_packets )
        {
            SDL_PauseAudioDevice( audioDeviceId, 0 );
            audioPacketQueue = &audio_packets;
        } );

    printf( "Playing " );

    if ( argc > 1 )
    {
        puts( argv[1] );
        player.play( argv[1] );
    }
    else
    {
        puts( "./res/sample2.ogv" );
        player.play( "./res/sample2.ogv" );
    }

    SDL_Quit();

    return 0;
}
