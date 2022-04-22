#include "theora-player.h"

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <iostream>
#include <vector>

theoraplayer::AudioPacket* audioPacket{};

static void SDLCALL audioCallback(void* userdata, Uint8* stream, int len) 
{
    if (!audioPacket)
        return;

    if (audioPacket->size == 0)
        return;

    len = (len > audioPacket->playms ? audioPacket->playms : len);

    SDL_MixAudio(stream, (Uint8*)audioPacket->audiobuf, len, SDL_MIX_MAXVOLUME);

    audioPacket->audiobuf += len;
    audioPacket->size -= len;
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
        [&]( const int width, const int height, theoraplayer::AudioPacket& audiopacket ) 
        {
            texture = SDL_CreateTexture( renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, width, height );

            SDL_AudioSpec audiospec{};
            audiospec.freq = audiopacket.freq;
            audiospec.format = AUDIO_S16SYS;
            audiospec.channels = audiopacket.channels;
            audiospec.samples = 2048;
            audiospec.callback = audioCallback;

            SDL_OpenAudio(&audiospec, &audiospec);

            audiopacket.freq = audiospec.freq;
            audiopacket.channels = audiospec.channels;
            audiopacket.size = audiospec.size;
            audiopacket.audiobuf = (int16_t*)malloc(audiospec.size);

            audioPacket = &audiopacket;
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

    player.setPlaySoundCallback(
        [&](const theoraplayer::AudioPacket& audioPacket)
        {
            SDL_PauseAudio(0);

            /*while (audioPacket.size > 0) {
                SDL_Delay(100);
            }

            SDL_CloseAudio();*/
        }
    );

    player.play( "./res/sample2.ogv" );

    SDL_Quit();

    return 0;
}
