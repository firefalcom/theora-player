#include "theora-player.h"

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <iostream>
#include <vector>
#include <queue>

std::queue< theoraplayer::AudioPacket > audioPacketQueue;
SDL_AudioDeviceID audioDeviceId;

static void SDLCALL audioCallback( void *userdata, Uint8 *stream, int len )
{
    if ( audioPacketQueue.empty() )
        return;

    const auto &packet = audioPacketQueue.front();
    audioPacketQueue.pop();

    memcpy( stream, packet.samples, packet.size );
}

int main()
{
    puts( "theora-player-test" );

    SDL_Init( SDL_INIT_VIDEO | SDL_INIT_AUDIO );

    auto window = SDL_CreateWindow( "theora-player-test", 0, 0, 640, 480, 0 );
    auto renderer = SDL_CreateRenderer( window, -1, 0 );

    SDL_Texture *texture;

    theoraplayer::Player player;

    player.setInitializeCallback(
        [&]( const int width, const int height, theoraplayer::AudioPacket &audioPacket )
        {
            texture = SDL_CreateTexture( renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, width, height );

            SDL_AudioSpec audiospec{};
            audiospec.freq = audioPacket.freq;
            audiospec.format = AUDIO_S16LSB;
            audiospec.channels = audioPacket.channels;
            audiospec.samples = 1024;
            audiospec.callback = audioCallback;

            audioDeviceId = SDL_OpenAudioDevice( nullptr, 0, &audiospec, &audiospec, 0 );

            if ( audioDeviceId < 0 )
            {
                puts( SDL_GetError() );
            }

            audioPacket.freq = audiospec.freq;
            audioPacket.channels = audiospec.channels;
            audioPacket.size = audiospec.size;
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

    player.setAudioUpdateCallback(
        [&]( const theoraplayer::AudioPacket &audioPacket )
        {
            SDL_PauseAudioDevice(audioDeviceId, 0);
            audioPacketQueue.push(audioPacket);
        } );

    player.setGetTicksCallback(
        []()
        {
            return SDL_GetTicks();
        } );

    player.play( "./res/sample2.ogv" );

    SDL_Quit();

    return 0;
}
