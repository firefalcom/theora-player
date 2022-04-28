#pragma once

#include <memory>
#include <functional>
#include <queue>
#include "theora/theoradec.h"

namespace theoraplayer
{
    using YCbCrBuffer = th_ycbcr_buffer;

    struct AudioSettings final
    {
        int maxMemorySize = 1024 * 1024 * 32;
        int frequency;
        int samples;
        int channels;
        int preloadSamplesCount = 128;
    };

    struct AudioPacket final
    {
        int16_t *data{ nullptr };
        uint32_t size;
    };

    using AudioPacketQueue = std::queue< AudioPacket >;

    struct VideoFrame final
    {
        YCbCrBuffer yuv;
        double time;

        void release()
        {
            for ( int i = 0; i < 3; i++ )
            {
                delete[] yuv[i].data;
            }
        }
    };

    class Player
    {
      public:
        Player();
        ~Player();

        void setInitializeCallback( std::function< void( const int, const int, const AudioSettings & ) > func );
        void setUpdateCallback( std::function< void( const YCbCrBuffer &, const int, const int ) > func );
        void setAudioUpdateCallback( std::function< void( AudioPacketQueue & ) > func );
        void play( const char *filepath );
        void stop();

      private:
        struct Pimpl;
        std::unique_ptr< Pimpl > pimpl;
    };

}
