#pragma once

#include <memory>
#include <functional>
#include "theora/theoradec.h"
#include <vector>

namespace theoraplayer
{
    struct AudioPacket final
    {
        uint32_t playms;
        int channels;
        int freq;
        int frames;
        float* samples;
        uint32_t size;
        int16_t* audiobuf;
    };

    struct AudioQueue final
    {

    };

    class Player
    {
      public:
        Player();
        ~Player();

        using YCbCrBuffer = th_ycbcr_buffer;

        void setInitializeCallback( std::function< void( const int, const int, AudioPacket& ) > func );
        void setUpdateCallback( std::function< void( const YCbCrBuffer &, const int, const int ) > func );
        void setPlaySoundCallback(std::function<void(const AudioPacket&)> func);
        void play( const char *filepath );
        void stop();

      private:
        struct Pimpl;
        std::unique_ptr< Pimpl > pimpl;
    };

}
