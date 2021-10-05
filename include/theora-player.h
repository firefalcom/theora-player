#pragma once

#include <memory>
#include <functional>

namespace theoraplayer
{
    void test();

    class Player
    {
      public:
        Player();

        void setUpdateCallback( std::function< void( uint8_t * ) > func );

      private:
        class Impl;
        std::unique_ptr< Impl > pimpl;
    };

}
