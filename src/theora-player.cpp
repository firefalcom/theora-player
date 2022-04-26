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
        ogg_sync_state syncState;
        ogg_page page;
        ogg_stream_state vstream;
        ogg_stream_state tstream;

        th_info tinfo;
        th_comment tcomment;
        th_dec_ctx *tdec = NULL;
        th_setup_info *tsetup = NULL;
        th_pixel_fmt pixelFormat;

        vorbis_info vinfo{};
        vorbis_dsp_state vdsp{};
        vorbis_block vblock{};
        vorbis_comment vcomment{};

        int theoraP = 0;
        int vorbisP = 0;
        int stateFlag = 0;

        int videobufReady = 0;
        ogg_int64_t videobufGranulepos = -1;
        double videobufTime = 0;

        ogg_int64_t audiobufGranulepos{ 0 };

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
        th_decode_ycbcr_out( tdec, yuv );

        updateCallback( yuv, width, height );
    }

    void Player::Pimpl::onAudioUpdate()
    {
        audioUpdateCallback( audioPacket );
    }

    int Player::Pimpl::queuePage( ogg_page *page )
    {
        if ( theoraP )
            ogg_stream_pagein( &tstream, page );
        if ( vorbisP )
            ogg_stream_pagein( &vstream, page );
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

        ogg_sync_init( &syncState );

        vorbis_info_init( &vinfo );
        vorbis_comment_init( &vcomment );

        th_comment_init( &tcomment );
        th_info_init( &tinfo );

        while ( !stateFlag )
        {
            int ret = buffer_data( infile, &syncState );
            if ( ret == 0 )
                break;
            while ( ogg_sync_pageout( &syncState, &page ) > 0 )
            {
                ogg_stream_state test;

                if ( !ogg_page_bos( &page ) )
                {
                    queuePage( &page );
                    stateFlag = 1;
                    break;
                }

                ogg_stream_init( &test, ogg_page_serialno( &page ) );
                ogg_stream_pagein( &test, &page );
                ogg_stream_packetout( &test, &op );

                if ( !theoraP && th_decode_headerin( &tinfo, &tcomment, &tsetup, &op ) >= 0 )
                {
                    memcpy( &tstream, &test, sizeof( test ) );
                    theoraP = 1;
                }
                else if ( !vorbisP && vorbis_synthesis_headerin( &vinfo, &vcomment, &op ) >= 0 )
                {
                    memcpy( &vstream, &test, sizeof( test ) );
                    vorbisP = 1;
                }
                else
                {
                    ogg_stream_clear( &test );
                }
            }
        }

        while ( ( theoraP && theoraP < 3 ) || ( vorbisP && vorbisP < 3 ) )
        {
            int ret;

            while ( theoraP && ( theoraP < 3 ) && ( ret = ogg_stream_packetout( &tstream, &op ) ) )
            {
                if ( ret < 0 )
                {
                    fprintf( stderr,
                        "Error parsing Theora stream headers; "
                        "corrupt stream?\n" );
                    exit( 1 );
                }
                if ( !th_decode_headerin( &tinfo, &tcomment, &tsetup, &op ) )
                {
                    fprintf( stderr,
                        "Error parsing Theora stream headers; "
                        "corrupt stream?\n" );
                    exit( 1 );
                }
                theoraP++;
            }

            while ( vorbisP && ( vorbisP < 3 ) && ( ret = ogg_stream_packetout( &vstream, &op ) ) )
            {
                if ( ret < 0 )
                {
                    fprintf( stderr, "Error parsing Vorbis stream headers; corrupt stream?\n" );
                    exit( 1 );
                }
                if ( vorbis_synthesis_headerin( &vinfo, &vcomment, &op ) )
                {
                    fprintf( stderr, "Error parsing Vorbis stream headers; corrupt stream?\n" );
                    exit( 1 );
                }
                vorbisP++;
                if ( vorbisP == 3 )
                    break;
            }

            if ( ogg_sync_pageout( &syncState, &page ) > 0 )
            {
                queuePage( &page );
            }
            else
            {
                int ret = buffer_data( infile, &syncState );
                if ( ret == 0 )
                {
                    fprintf( stderr, "End of file while searching for codec headers.\n" );
                    exit( 1 );
                }
            }
        }

        if ( theoraP )
        {
            tdec = th_decode_alloc( &tinfo, tsetup );
            pixelFormat = tinfo.pixel_fmt;

            assert( tinfo.pixel_fmt == TH_PF_420 );

            th_decode_ctl( tdec, TH_DECCTL_GET_PPLEVEL_MAX, &pp_level_max, sizeof( pp_level_max ) );
            pp_level = pp_level_max;
            th_decode_ctl( tdec, TH_DECCTL_SET_PPLEVEL, &pp_level, sizeof( pp_level ) );
            pp_inc = 0;
        }
        else
        {
            th_info_clear( &tinfo );
            th_comment_clear( &tcomment );
        }

        th_setup_free( tsetup );

        assert( theoraP );

        if ( vorbisP )
        {
            vorbis_synthesis_init( &vdsp, &vinfo );
            vorbis_block_init( &vdsp, &vblock );

            fprintf( stdout,
                "Ogg logical stream %lx is Vorbis %d channel %ld Hz audio.\n",
                vstream.serialno,
                vinfo.channels,
                vinfo.rate );
        }
        else
        {
            vorbis_info_clear( &vinfo );
            vorbis_comment_clear( &vcomment );
        }

        stateFlag = 0;

        width = tinfo.pic_width;
        height = tinfo.pic_height;

        audioPacket.channels = vinfo.channels;
        audioPacket.freq = vinfo.rate;

        initCallback( width, height, audioPacket );

        int audio_frames{ 0 };

        const uint32_t base_ticks = getTicksCallback();

        while ( playing )
        {
            bool need_pages = false;

            while ( vorbisP )
            {
                float **pcm{ nullptr };
                const int frames = vorbis_synthesis_pcmout( &vdsp, &pcm );

                if ( frames > 0 )
                {
                    audioPacket.size = frames * vinfo.channels * sizeof( int16_t );
                    audioPacket.samples = new int16_t[audioPacket.size]{};
                    audioPacket.frames = frames;

                    audioPacket.playms = static_cast< unsigned long >( ( static_cast< double >( audio_frames )
                        / static_cast<double>( vinfo.rate ) * 1000.0 ) );

                    auto count = 0;

                    for ( i = 0; i < frames; i++ )
                    {
                        for ( j = 0; j < vinfo.channels; j++ )
                        {
                            int val = rint( pcm[j][i] * 32767.f );
                            if ( val > 32767 )
                                val = 32767;
                            if ( val < -32768 )
                                val = -32768;
                            audioPacket.samples[count++] = val;
                        }
                    }
                    
                    vorbis_synthesis_read( &vdsp, frames );
                    audio_frames += frames;

                    const uint32_t now{ getTicksCallback() - base_ticks };
                    onAudioUpdate();
                    if (audioPacket.playms >= (now + 2000))
                        break;

                    if ( vdsp.granulepos >= 0 )
                        audiobufGranulepos = vdsp.granulepos - frames + i;
                    else
                        audiobufGranulepos += i;
                }
                else
                {
                    if ( ogg_stream_packetout( &vstream, &op ) > 0 )
                    {
                        if ( vorbis_synthesis( &vblock, &op ) == 0 )
                            vorbis_synthesis_blockin( &vdsp, &vblock );
                    }
                    else
                    {
                        if ( !theoraP )
                            need_pages = true;
                        break;
                    }
                }
            }

            if ( theoraP && !videobufReady )
            {
                if ( ogg_stream_packetout( &tstream, &op ) > 0 )
                {
                    if ( pp_inc )
                    {
                        pp_level += pp_inc;
                        th_decode_ctl( tdec, TH_DECCTL_SET_PPLEVEL, &pp_level, sizeof( pp_level ) );
                        pp_inc = 0;
                    }
                    if ( op.granulepos >= 0 )
                    {
                        th_decode_ctl( tdec, TH_DECCTL_SET_GRANPOS, &op.granulepos, sizeof( op.granulepos ) );
                    }
                    if ( th_decode_packetin( tdec, &op, &videobufGranulepos ) == 0 )
                    {
                        videobufTime = th_granule_time( tdec, videobufGranulepos );
                        frames++;

                        if ( videobufTime >= get_time() )
                            videobufReady = 1;
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
                }
            }

            if ( feof( infile ) )
            {
                break;
            }

            if ( need_pages )
            {
                buffer_data( infile, &syncState );

                while ( ogg_sync_pageout( &syncState, &page ) > 0 )
                {
                    queuePage( &page );
                }
            }

            if ( stateFlag && videobufReady && videobufTime <= get_time() )
            {
                onVideoUpdate();
                videobufReady = 0;
            }

            if ( stateFlag && vorbisP && ( videobufReady || !theoraP ) )
            {
                struct timeval timeout;
                fd_set writefs;
                fd_set empty;
                int n = 0;

                FD_ZERO( &writefs );
                FD_ZERO( &empty );

                if ( theoraP )
                {
                    double tdiff;
                    long milliseconds;
                    tdiff = videobufTime - get_time();

                    if ( tdiff > tinfo.fps_denominator * 0.25 / tinfo.fps_numerator )
                    {
                        pp_inc = pp_level < pp_level_max ? 1 : 0;
                    }
                    else if ( tdiff < tinfo.fps_denominator * 0.05 / tinfo.fps_numerator )
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

            if ( ( !theoraP || videobufReady ) )
                stateFlag = 1;
            if ( feof( infile ) )
                stateFlag = 1;
        }

        if ( vorbisP )
        {
            ogg_stream_clear( &vstream );
            vorbis_block_clear( &vblock );
            vorbis_dsp_clear( &vdsp );
            vorbis_comment_clear( &vcomment );
            vorbis_info_clear( &vinfo );
        }
        if ( theoraP )
        {
            ogg_stream_clear( &tstream );
            th_decode_free( tdec );
            th_comment_clear( &tcomment );
            th_info_clear( &tinfo );
        }
        ogg_sync_clear( &syncState );

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

    void Player::setGetTicksCallback( std::function< uint32_t() > func )
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
