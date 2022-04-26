#include "theora-player.h"

#include "theora/theoradec.h"
#include "vorbis/codec.h"

#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined( WIN32 )
#    include <WinSock2.h>
#    include <io.h>
#else
#    include <sys/time.h>
#    include <unistd.h>
#endif

#include <chrono>

namespace theoraplayer
{

    struct Player::Pimpl
    {
        ogg_sync_state oy;
        ogg_page og;
        ogg_stream_state vo;
        ogg_stream_state to;

        th_info ti;
        th_comment tc;
        th_dec_ctx *td = NULL;
        th_setup_info *ts = NULL;
        th_pixel_fmt px_fmt;

        vorbis_info vi{};
        vorbis_dsp_state vd{};
        vorbis_block vb{};
        vorbis_comment vc{};

        int theora_p = 0;
        int vorbis_p = 0;
        int stateflag = 0;

        int videobuf_ready = 0;
        ogg_int64_t videobuf_granulepos = -1;
        double videobuf_time = 0;

        ogg_int64_t audiobuf_granulepos{ 0 };

        AudioPacket audioPacket{};

        int width, height;
        std::function< void( const int, const int, AudioPacket &audioPacket ) > initCallback;
        std::function< void( const Player::YCbCrBuffer &, const int, const int ) > updateCallback;
        std::function< void( const AudioPacket & ) > audioUpdateCallback;
        std::function< uint32_t() > getTicksCallback;

        void onVideoUpdate();
        void onAudioUpdate();
        int queuePage( ogg_page * );
        bool playing{ false };
        void play( const char * );
        void stop();
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
        th_ycbcr_buffer yuv;
        th_decode_ycbcr_out( td, yuv );

        updateCallback( yuv, width, height );
    }

    void Player::Pimpl::onAudioUpdate()
    {
        audioUpdateCallback( audioPacket );
    }

    int Player::Pimpl::queuePage( ogg_page *page )
    {
        if ( theora_p )
            ogg_stream_pagein( &to, page );
        if ( vorbis_p )
            ogg_stream_pagein( &vo, page );
        return 0;
    }

    void Player::Pimpl::play( const char *filepath )
    {
        puts( "play" );
        playing = true;

        int pp_level_max;
        int pp_level;
        int pp_inc{ 0 };
        int i, j;
        ogg_packet op;

        FILE *infile = nullptr;

        int frames = 0;
        int dropped = 0;

#ifdef _WIN32
        _setmode( _fileno( stdin ), _O_BINARY );
#endif

        infile = fopen( filepath, "rb" );

        assert( infile != nullptr );

        ogg_sync_init( &oy );

        vorbis_info_init( &vi );
        vorbis_comment_init( &vc );

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
                    queuePage( &og );
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
                else if ( !vorbis_p && vorbis_synthesis_headerin( &vi, &vc, &op ) >= 0 )
                {
                    memcpy( &vo, &test, sizeof( test ) );
                    vorbis_p = 1;
                }
                else
                {
                    ogg_stream_clear( &test );
                }
            }
        }

        while ( ( theora_p && theora_p < 3 ) || ( vorbis_p && vorbis_p < 3 ) )
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

            while ( vorbis_p && ( vorbis_p < 3 ) && ( ret = ogg_stream_packetout( &vo, &op ) ) )
            {
                if ( ret < 0 )
                {
                    fprintf( stderr, "Error parsing Vorbis stream headers; corrupt stream?\n" );
                    exit( 1 );
                }
                if ( vorbis_synthesis_headerin( &vi, &vc, &op ) )
                {
                    fprintf( stderr, "Error parsing Vorbis stream headers; corrupt stream?\n" );
                    exit( 1 );
                }
                vorbis_p++;
                if ( vorbis_p == 3 )
                    break;
            }

            if ( ogg_sync_pageout( &oy, &og ) > 0 )
            {
                queuePage( &og );
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

        if ( vorbis_p )
        {
            vorbis_synthesis_init( &vd, &vi );
            vorbis_block_init( &vd, &vb );

            fprintf( stdout,
                "Ogg logical stream %lx is Vorbis %d channel %ld Hz audio.\n",
                vo.serialno,
                vi.channels,
                vi.rate );
        }
        else
        {
            vorbis_info_clear( &vi );
            vorbis_comment_clear( &vc );
        }

        stateflag = 0;

        width = ti.pic_width;
        height = ti.pic_height;

        audioPacket.channels = vi.channels;
        audioPacket.freq = vi.rate;

        initCallback( width, height, audioPacket );

        int audio_frames{ 0 };

        const uint32_t baseTicks = getTicksCallback();

        while ( playing )
        {
            bool need_pages = false;

            while ( vorbis_p )
            {
                float **pcm{ nullptr };
                const int frames = vorbis_synthesis_pcmout( &vd, &pcm );

                if ( frames > 0 )
                {
                    audioPacket.size = frames * vi.channels * sizeof( int16_t );
                    audioPacket.samples = new int16_t[audioPacket.size]{};
                    audioPacket.frames = frames;

                    audioPacket.playms = static_cast< unsigned long >( ( static_cast< double >( audio_frames )
                        / static_cast<double>( vi.rate ) * 1000.0 ) );

                    auto count = 0;

                    for ( i = 0; i < frames; i++ )
                    {
                        for ( j = 0; j < vi.channels; j++ )
                        {
                            int val = rint( pcm[j][i] * 32767.f );
                            if ( val > 32767 )
                                val = 32767;
                            if ( val < -32768 )
                                val = -32768;
                            audioPacket.samples[count++] = val;
                        }
                    }
                    
                    vorbis_synthesis_read( &vd, frames );
                    audio_frames += frames;

                    const uint32_t now{ getTicksCallback() - baseTicks };
                    onAudioUpdate();
                    if (audioPacket.playms >= (now + 2000))
                        break;

                    if ( vd.granulepos >= 0 )
                        audiobuf_granulepos = vd.granulepos - frames + i;
                    else
                        audiobuf_granulepos += i;
                }
                else
                {
                    if ( ogg_stream_packetout( &vo, &op ) > 0 )
                    {
                        if ( vorbis_synthesis( &vb, &op ) == 0 )
                            vorbis_synthesis_blockin( &vd, &vb );
                    }
                    else
                    {
                        if ( !theora_p )
                            need_pages = true;
                        break;
                    }
                }
            }

            if ( theora_p && !videobuf_ready )
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
                {
                    need_pages = true;
                    //                    break;
                }
            }

            if ( feof( infile ) )
            {
                break;
            }

            if ( need_pages )
            {
                buffer_data( infile, &oy );

                while ( ogg_sync_pageout( &oy, &og ) > 0 )
                {
                    queuePage( &og );
                }
            }

            if ( stateflag && videobuf_ready && videobuf_time <= get_time() )
            {
                onVideoUpdate();
                videobuf_ready = 0;
            }

            if ( stateflag && vorbis_p && ( videobuf_ready || !theora_p ) )
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

        if ( vorbis_p )
        {
            ogg_stream_clear( &vo );
            vorbis_block_clear( &vb );
            vorbis_dsp_clear( &vd );
            vorbis_comment_clear( &vc );
            vorbis_info_clear( &vi );
        }
        if ( theora_p )
        {
            ogg_stream_clear( &to );
            th_decode_free( td );
            th_comment_clear( &tc );
            th_info_clear( &ti );
        }
        ogg_sync_clear( &oy );

        delete[] audioPacket.samples;

        if ( infile && infile != stdin )
            fclose( infile );
    }

    void Player::Pimpl::stop()
    {
        playing = false;
    }

    Player::Player() : pimpl( new Pimpl() )
    {
    }

    Player::~Player() = default;

    void Player::setInitializeCallback( std::function< void( const int, const int, AudioPacket & ) > func )
    {
        pimpl->initCallback = func;
    }

    void Player::setUpdateCallback( std::function< void( const Player::YCbCrBuffer &, const int, const int ) > func )
    {
        pimpl->updateCallback = func;
    }

    void Player::setAudioUpdateCallback( std::function< void( const AudioPacket & ) > func )
    {
        pimpl->audioUpdateCallback = func;
    }

    void Player::setGetTicksCallback(std::function< uint32_t() > func)
    {
        pimpl->getTicksCallback = func;
    }

    void Player::play( const char *filepath )
    {
        pimpl->play( filepath );
    }

    void Player::stop()
    {
        pimpl->stop();
    }
}
