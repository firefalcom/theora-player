#include "theora-player.h"

#include "theora/theoradec.h"
#include "vorbis/codec.h"

#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined( _WIN32 )
#    include <io.h>
#else
#    include <sys/time.h>
#    include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <queue>

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
        double videoTime = 0;

        int16_t *audioBuffer;
        AudioSettings audioSettings;

        int width, height;
        std::function< void( const int, const int, const AudioSettings & ) > initCallback;
        std::function< void( const YCbCrBuffer &, const int, const int ) > updateCallback;
        std::function< void( AudioPacketQueue & ) > audioUpdateCallback;

        void onVideoUpdate();

        int queuePage( ogg_page * );
        bool playing{ false };
        bool playStarted = false;

        void play( const char * );
        void stop();

        std::queue< theoraplayer::AudioPacket > audioPacketQueue;
        std::queue< theoraplayer::VideoFrame > videoFrameQueue;
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

        audioSettings.channels = vinfo.channels;
        audioSettings.frequency = vinfo.rate;
        audioSettings.samples = 1024;

        initCallback( width, height, audioSettings );

        uint32_t audio_buffer_size = sizeof( int16_t ) * audioSettings.samples * audioSettings.channels;

        int16_t *audio_buffer = new int16_t[audioSettings.maxMemorySize / 2];

        int16_t *audio_write_pointer = audio_buffer;
        int16_t *audio_read_pointer = audio_buffer;

        while ( playing )
        {
            bool need_pages = false;

            while ( vorbisP )
            {
                float **pcm{ nullptr };
                const int frames = vorbis_synthesis_pcmout( &vdsp, &pcm );

                if ( frames > 0 )
                {
                    auto size = frames * vinfo.channels * sizeof( int16_t );
                    auto count = 0;
                    auto destination = audio_write_pointer;

                    for ( i = 0; i < frames; i++ )
                    {
                        for ( j = 0; j < vinfo.channels; j++ )
                        {
                            int val = std::clamp( static_cast< int >( pcm[j][i] * 32767.f ), -32768, 32768 );
                            audio_write_pointer[count++] = val;
                        }
                    }

                    audio_write_pointer += count;

                    while ( audio_read_pointer < audio_write_pointer - audioSettings.samples * audioSettings.channels )
                    {
                        audioPacketQueue.push( { audio_read_pointer, audio_buffer_size } );

                        audio_read_pointer += audioSettings.samples * audioSettings.channels;
                    }

                    vorbis_synthesis_read( &vdsp, frames );
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
                        videoTime = th_granule_time( tdec, videobufGranulepos );
                        frames++;
                        videobufReady = 1;
                    }
                }
                else
                {
                    need_pages = true;
                }
            }

            if ( need_pages )
            {
                buffer_data( infile, &syncState );

                while ( ogg_sync_pageout( &syncState, &page ) > 0 )
                {
                    queuePage( &page );
                }
            }

            if ( videobufReady )
            {
                th_ycbcr_buffer yuv;
                th_decode_ycbcr_out( tdec, yuv );

                VideoFrame frame;

                for ( int i = 0; i < 3; i++ )
                {
                    frame.yuv[i].width = yuv[i].width;
                    frame.yuv[i].height = yuv[i].height;
                    frame.yuv[i].stride = yuv[i].stride;
                    auto size = yuv[i].height * yuv[i].stride;
                    frame.yuv[i].data = new unsigned char[size];
                    memcpy( frame.yuv[i].data, yuv[i].data, size );
                }

                frame.time = videoTime;

                videoFrameQueue.push( frame );

                videobufReady = 0;
            }

            if ( feof( infile ) )
            {
                stateFlag = 1;
            }

            if ( !playStarted )
            {
                if ( !vorbisP )
                {
                    playStarted = true;
                }
                else
                {
                    if ( audioPacketQueue.size() > audioSettings.preloadSamplesCount )
                    {
                        audioUpdateCallback( audioPacketQueue );

                        playStarted = true;

                        get_time_start = std::chrono::high_resolution_clock::now();
                    }
                }
            }

            if ( playStarted )
            {
                auto now = get_time();

                while ( !videoFrameQueue.empty() )
                {
                    auto frame = videoFrameQueue.front();

                    if ( frame.time < now )
                    {
                        updateCallback( frame.yuv, width, height );
                        videoFrameQueue.pop();

                        frame.release();
                    }
                    else
                    {
                        break;
                    }
                }

                if ( videoFrameQueue.empty() && stateFlag )
                {
                    playing = false;
                }
            }
        }

        delete[] audio_buffer;

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

    void Player::setInitializeCallback( std::function< void( const int, const int, const AudioSettings & ) > func )
    {
        pimpl->initCallback = func;
    }

    void Player::setUpdateCallback( std::function< void( const YCbCrBuffer &, const int, const int ) > func )
    {
        pimpl->updateCallback = func;
    }

    void Player::setAudioUpdateCallback( std::function< void( AudioPacketQueue & ) > func )
    {
        pimpl->audioUpdateCallback = func;
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
