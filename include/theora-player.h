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
        uint32_t size;
        int16_t *samples{ nullptr };
    };

    class Player
    {
      public:
        Player();
        ~Player();

        using YCbCrBuffer = th_ycbcr_buffer;

        void setInitializeCallback( std::function< void( const int, const int, AudioPacket & ) > func );
        void setUpdateCallback( std::function< void( const YCbCrBuffer &, const int, const int ) > func );
        void setAudioUpdateCallback( std::function< void( const AudioPacket & ) > func );
        void setGetTicksCallback( std::function< uint32_t() > func );
        void play( const char *filepath );
        void stop();

      private:
        struct Pimpl;
        std::unique_ptr< Pimpl > pimpl;
    };

}
