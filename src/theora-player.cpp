#include "theora-player.h"

#include "theora/theoradec.h"

#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <SDL2/SDL.h>

namespace theoraplayer
{

    struct Player::Pimpl
    {
        SDL_Window *window;
        SDL_Surface *surface;
        ogg_sync_state oy;
        ogg_page og;
        ogg_stream_state vo;
        ogg_stream_state to;
        th_info ti;
        th_comment tc;
        th_dec_ctx *td = NULL;
        th_setup_info *ts = NULL;
        th_pixel_fmt px_fmt;

        int theora_p = 0;
        int stateflag = 0;

        int videobuf_ready = 0;
        ogg_int64_t videobuf_granulepos = -1;
        double videobuf_time = 0;

        void onVideoUpdate();
        int queue_page( ogg_page * );
        void play( const char * );
    };

    int buffer_data( FILE *in, ogg_sync_state *oy )
    {
        char *buffer = ogg_sync_buffer( oy, 4096 );
        int bytes = fread( buffer, 1, 4096, in );
        ogg_sync_wrote( oy, bytes );
        return ( bytes );
    }

    auto get_time_start = std::chrono::high_resolution_clock::now();

    double get_time()
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto delta = now - get_time_start;
        auto ms = std::chrono::duration_cast< std::chrono::milliseconds >( delta ).count();
        return ms / 1000.0;
    }

    void Player::Pimpl::onVideoUpdate()
    {
        int i;
        th_ycbcr_buffer yuv;
        int y_offset, uv_offset;
        th_decode_ycbcr_out( td, yuv );

        /* printf( "%i\n", surface->w ); */

        y_offset = ( ti.pic_x & ~1 ) + yuv[0].stride * ( ti.pic_y & ~1 );

        SDL_LockSurface( surface );

        for ( i = 0; i < surface->h; i++ )
            memcpy( ( uint8_t * )surface->pixels + i * surface->pitch,
                yuv[0].data + y_offset + yuv[0].stride * i,
                surface->w );

        SDL_UnlockSurface( surface );

        SDL_UpdateWindowSurface( window );
    }

    int Player::Pimpl::queue_page( ogg_page *page )
    {
        if ( theora_p )
            ogg_stream_pagein( &to, page );
        return 0;
    }

    void Player::Pimpl::play( const char *filepath )
    {

        int pp_level_max;
        int pp_level;
        int pp_inc{ 0 };
        int i;
        ogg_packet op;

        FILE *infile = nullptr;

        int frames = 0;
        int dropped = 0;

        SDL_Init( SDL_INIT_VIDEO );
        window = SDL_CreateWindow( "plop", 0, 0, 640, 480, 0 );
        surface = SDL_GetWindowSurface( window );

#ifdef _WIN32
        _setmode( _fileno( stdin ), _O_BINARY );
#endif

        infile = fopen( "./res/sample.ogv", "rb" );

        assert( infile != nullptr );

        ogg_sync_init( &oy );

        th_comment_init( &tc );
        th_info_init( &ti );

        while ( !stateflag )
        {
            int ret = buffer_data( infile, &oy );
            if ( ret == 0 )
                break;
            while ( ogg_sync_pageout( &oy, &og ) > 0 )
            {
                ogg_stream_state test;

                if ( !ogg_page_bos( &og ) )
                {
                    queue_page( &og );
                    stateflag = 1;
                    break;
                }

                ogg_stream_init( &test, ogg_page_serialno( &og ) );
                ogg_stream_pagein( &test, &og );
                ogg_stream_packetout( &test, &op );

                if ( !theora_p && th_decode_headerin( &ti, &tc, &ts, &op ) >= 0 )
                {
                    memcpy( &to, &test, sizeof( test ) );
                    theora_p = 1;
                }
                else
                {
                    ogg_stream_clear( &test );
                }
            }
        }

        while ( ( theora_p && theora_p < 3 ) )
        {
            int ret;

            while ( theora_p && ( theora_p < 3 ) && ( ret = ogg_stream_packetout( &to, &op ) ) )
            {
                if ( ret < 0 )
                {
                    fprintf( stderr,
                        "Error parsing Theora stream headers; "
                        "corrupt stream?\n" );
                    exit( 1 );
                }
                if ( !th_decode_headerin( &ti, &tc, &ts, &op ) )
                {
                    fprintf( stderr,
                        "Error parsing Theora stream headers; "
                        "corrupt stream?\n" );
                    exit( 1 );
                }
                theora_p++;
            }

            if ( ogg_sync_pageout( &oy, &og ) > 0 )
            {
                queue_page( &og );
            }
            else
            {
                int ret = buffer_data( infile, &oy );
                if ( ret == 0 )
                {
                    fprintf( stderr, "End of file while searching for codec headers.\n" );
                    exit( 1 );
                }
            }
        }

        if ( theora_p )
        {
            td = th_decode_alloc( &ti, ts );
            px_fmt = ti.pixel_fmt;

            assert( ti.pixel_fmt == TH_PF_420 );

            th_decode_ctl( td, TH_DECCTL_GET_PPLEVEL_MAX, &pp_level_max, sizeof( pp_level_max ) );
            pp_level = pp_level_max;
            th_decode_ctl( td, TH_DECCTL_SET_PPLEVEL, &pp_level, sizeof( pp_level ) );
            pp_inc = 0;
        }
        else
        {
            th_info_clear( &ti );
            th_comment_clear( &tc );
        }

        th_setup_free( ts );

        assert( theora_p );

        stateflag = 0;

        while ( true ) // :TODO:
        {

            while ( theora_p && !videobuf_ready )
            {
                if ( ogg_stream_packetout( &to, &op ) > 0 )
                {

                    if ( pp_inc )
                    {
                        pp_level += pp_inc;
                        th_decode_ctl( td, TH_DECCTL_SET_PPLEVEL, &pp_level, sizeof( pp_level ) );
                        pp_inc = 0;
                    }
                    if ( op.granulepos >= 0 )
                    {
                        th_decode_ctl( td, TH_DECCTL_SET_GRANPOS, &op.granulepos, sizeof( op.granulepos ) );
                    }
                    if ( th_decode_packetin( td, &op, &videobuf_granulepos ) == 0 )
                    {
                        videobuf_time = th_granule_time( td, videobuf_granulepos );
                        frames++;

                        if ( videobuf_time >= get_time() )
                            videobuf_ready = 1;
                        else
                        {
                            pp_inc = pp_level > 0 ? -1 : 0;
                            dropped++;
                        }
                    }
                }
                else
                    break;
            }

            if ( !videobuf_ready && feof( infile ) )
                break;

            if ( !videobuf_ready )
            {
                buffer_data( infile, &oy );
                while ( ogg_sync_pageout( &oy, &og ) > 0 )
                {
                    queue_page( &og );
                }
            }

            if ( stateflag && videobuf_ready && videobuf_time <= get_time() )
            {
                onVideoUpdate();
                videobuf_ready = 0;
            }

            if ( stateflag && ( videobuf_ready || !theora_p ) )
            {
                struct timeval timeout;
                fd_set writefs;
                fd_set empty;
                int n = 0;

                FD_ZERO( &writefs );
                FD_ZERO( &empty );

                if ( theora_p )
                {
                    double tdiff;
                    long milliseconds;
                    tdiff = videobuf_time - get_time();

                    if ( tdiff > ti.fps_denominator * 0.25 / ti.fps_numerator )
                    {
                        pp_inc = pp_level < pp_level_max ? 1 : 0;
                    }
                    else if ( tdiff < ti.fps_denominator * 0.05 / ti.fps_numerator )
                    {
                        pp_inc = pp_level > 0 ? -1 : 0;
                    }
                    milliseconds = tdiff * 1000 - 5;
                    if ( milliseconds > 500 )
                        milliseconds = 500;
                    if ( milliseconds > 0 )
                    {
                        timeout.tv_sec = milliseconds / 1000;
                        timeout.tv_usec = ( milliseconds % 1000 ) * 1000;
                    }
                }
                else
                {
                    select( n, &empty, &writefs, &empty, NULL );
                }
            }

            if ( ( !theora_p || videobuf_ready ) )
                stateflag = 1;
            if ( feof( infile ) )
                stateflag = 1;
        }

        if ( theora_p )
        {
            ogg_stream_clear( &to );
            th_decode_free( td );
            th_comment_clear( &tc );
            th_info_clear( &ti );
        }
        ogg_sync_clear( &oy );

        if ( infile && infile != stdin )
            fclose( infile );

    }

    Player::Player() : pimpl( new Pimpl() )
    {
    }

    Player::~Player() = default;

    void Player::play( const char *filepath )
    {
        pimpl->play( filepath );
    }
}
