#pragma once

#include <memory>
#include <functional>

namespace theoraplayer
{
    class Player
    {
      public:
        Player();
        ~Player();

        void setUpdateCallback( std::function< void( uint8_t * ) > func );
        void play( const char *filepath );

      private:
        struct Pimpl;
        std::unique_ptr< Pimpl > pimpl;
    };

}
