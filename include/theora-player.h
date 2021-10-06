#pragma once

#include <memory>
#include <functional>
#include "theora/theoradec.h"

namespace theoraplayer
{
    class Player
    {
      public:
        Player();
        ~Player();

        using YCbCrBuffer = th_ycbcr_buffer;

        void setInitializeCallback( std::function< void( const int, const int ) > func );
        void setUpdateCallback( std::function< void( const YCbCrBuffer &, const int, const int ) > func );
        void play( const char *filepath );
        void stop();

      private:
        struct Pimpl;
        std::unique_ptr< Pimpl > pimpl;
    };

}
