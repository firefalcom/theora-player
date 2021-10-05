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

#include <SDL2/SDL.h>

SDL_Window *window;
SDL_Surface *surface;

/* Helper; just grab some more compressed bitstream and sync it for
   page extraction */
int buffer_data( FILE *in, ogg_sync_state *oy )
{
    char *buffer = ogg_sync_buffer( oy, 4096 );
    int bytes = fread( buffer, 1, 4096, in );
    ogg_sync_wrote( oy, bytes );
    return ( bytes );
}

double get_time()
{
    return SDL_GetTicks() / 1000.0;
}

/* never forget that globals are a one-way ticket to Hell */
/* Ogg and codec state for demux/decode */
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

#define OC_CLAMP255( _x ) ( ( unsigned char )( ( ( ( _x ) < 0 ) - 1 ) & ( ( _x ) | -( ( _x ) > 255 ) ) ) )

/* single frame video buffering */
int videobuf_ready = 0;
ogg_int64_t videobuf_granulepos = -1;
double videobuf_time = 0;

/* clean quit on Ctrl-C for SDL and thread shutdown as per SDL example
   (we don't use any threads, but libSDL does) */
int got_sigint = 0;
static void sigint_handler( int signal )
{
    got_sigint = 1;
}

static void open_video( void )
{
    int w;
    int h;
    w = ( ti.pic_x + ti.pic_width + 1 & ~1 ) - ( ti.pic_x & ~1 );
    h = ( ti.pic_y + ti.pic_height + 1 & ~1 ) - ( ti.pic_y & ~1 );
}

static void video_write( void )
{
    int i;
    th_ycbcr_buffer yuv;
    int y_offset, uv_offset;
    th_decode_ycbcr_out( td, yuv );

    printf( "%i\n", surface->w );

    y_offset = ( ti.pic_x & ~1 ) + yuv[0].stride * ( ti.pic_y & ~1 );

    SDL_LockSurface( surface );

    for ( i = 0; i < surface->h; i++ )
        memcpy(
            ( uint8_t * )surface->pixels + i * surface->pitch, yuv[0].data + y_offset + yuv[0].stride * i, surface->w );

    SDL_UnlockSurface( surface );

    SDL_UpdateWindowSurface( window );
}
/* dump the theora (or vorbis) comment header */
static int dump_comments( th_comment *tc )
{
    int i, len;
    char *value;
    FILE *out = stdout;

    fprintf( out, "Encoded by %s\n", tc->vendor );
    if ( tc->comments )
    {
        fprintf( out, "theora comment header:\n" );
        for ( i = 0; i < tc->comments; i++ )
        {
            if ( tc->user_comments[i] )
            {
                len = tc->comment_lengths[i];
                value = ( char * )malloc( len + 1 );
                memcpy( value, tc->user_comments[i], len );
                value[len] = '\0';
                fprintf( out, "\t%s\n", value );
                free( value );
            }
        }
    }
    return ( 0 );
}

static void report_colorspace( th_info *ti )
{
    switch ( ti->colorspace )
    {
        case TH_CS_UNSPECIFIED:
            /* nothing to report */
            break;
            ;
        case TH_CS_ITU_REC_470M:
            fprintf( stderr, "  encoder specified ITU Rec 470M (NTSC) color.\n" );
            break;
            ;
        case TH_CS_ITU_REC_470BG:
            fprintf( stderr, "  encoder specified ITU Rec 470BG (PAL) color.\n" );
            break;
            ;
        default:
            fprintf( stderr, "warning: encoder specified unknown colorspace (%d).\n", ti->colorspace );
            break;
            ;
    }
}

static int queue_page( ogg_page *page )
{
    if ( theora_p )
        ogg_stream_pagein( &to, page );
    return 0;
}

static void usage( void )
{
    fprintf( stderr,
        "Usage: player_example <file.ogv>\n"
        "input is read from stdin if no file is passed on the command line\n"
        "\n" );
}

int test2()
{

    int pp_level_max;
    int pp_level;
    int pp_inc;
    int i;
    ogg_packet op;

    FILE *infile = stdin;

    int frames = 0;
    int dropped = 0;

    SDL_Init( SDL_INIT_VIDEO );
    window = SDL_CreateWindow( "plop", 0, 0, 640, 480, 0 );
    surface = SDL_GetWindowSurface( window );

#ifdef _WIN32 /* We need to set stdin/stdout to binary mode. Damn windows. */
    /* Beware the evil ifdef. We avoid these where we can, but this one we
       cannot. Don't add any more, you'll probably go to hell if you do. */
    _setmode( _fileno( stdin ), _O_BINARY );
#endif

    infile = fopen( "./res/sample.ogv", "rb" );

    assert( infile != nullptr );

    /* start up Ogg stream synchronization layer */
    ogg_sync_init( &oy );

    /* init supporting Vorbis structures needed in header parsing */

    /* init supporting Theora structures needed in header parsing */
    th_comment_init( &tc );
    th_info_init( &ti );

    /* Ogg file open; parse the headers */
    /* Only interested in Vorbis/Theora streams */
    while ( !stateflag )
    {
        int ret = buffer_data( infile, &oy );
        if ( ret == 0 )
            break;
        while ( ogg_sync_pageout( &oy, &og ) > 0 )
        {
            ogg_stream_state test;

            /* is this a mandated initial header? If not, stop parsing */
            if ( !ogg_page_bos( &og ) )
            {
                /* don't leak the page; get it into the appropriate stream */
                queue_page( &og );
                stateflag = 1;
                break;
            }

            ogg_stream_init( &test, ogg_page_serialno( &og ) );
            ogg_stream_pagein( &test, &og );
            ogg_stream_packetout( &test, &op );

            /* identify the codec: try theora */
            if ( !theora_p && th_decode_headerin( &ti, &tc, &ts, &op ) >= 0 )
            {
                /* it is theora */
                memcpy( &to, &test, sizeof( test ) );
                theora_p = 1;
            }
            else
            {
                /* whatever it is, we don't care about it */
                ogg_stream_clear( &test );
            }
        }
        /* fall through to non-bos page parsing */
    }

    /* we're expecting more header packets. */
    while ( ( theora_p && theora_p < 3 ) )
    {
        int ret;

        /* look for further theora headers */
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

        /* The header pages/packets will arrive before anything else we
           care about, or the stream is not obeying spec */

        if ( ogg_sync_pageout( &oy, &og ) > 0 )
        {
            queue_page( &og ); /* demux into the appropriate stream */
        }
        else
        {
            int ret = buffer_data( infile, &oy ); /* someone needs more data */
            if ( ret == 0 )
            {
                fprintf( stderr, "End of file while searching for codec headers.\n" );
                exit( 1 );
            }
        }
    }

    /* and now we have it all.  initialize decoders */
    if ( theora_p )
    {
        td = th_decode_alloc( &ti, ts );
        printf( "Ogg logical stream %lx is Theora %dx%d %.02f fps",
            to.serialno,
            ti.pic_width,
            ti.pic_height,
            ( double )ti.fps_numerator / ti.fps_denominator );
        px_fmt = ti.pixel_fmt;
        switch ( ti.pixel_fmt )
        {
            case TH_PF_420:
                printf( " 4:2:0 video\n" );
                break;
            case TH_PF_422:
                printf( " 4:2:2 video\n" );
                break;
            case TH_PF_444:
                printf( " 4:4:4 video\n" );
                break;
            case TH_PF_RSVD:
            default:
                printf( " video\n  (UNKNOWN Chroma sampling!)\n" );
                break;
        }
        if ( ti.pic_width != ti.frame_width || ti.pic_height != ti.frame_height )
            printf( "  Frame content is %dx%d with offset (%d,%d).\n",
                ti.frame_width,
                ti.frame_height,
                ti.pic_x,
                ti.pic_y );
        report_colorspace( &ti );
        dump_comments( &tc );
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

    if ( theora_p )
        open_video();

    stateflag = 0;
    while ( !got_sigint )
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
                        /*If we are too slow, reduce the pp level.*/
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
            /* no data yet for somebody.  Grab another page */
            buffer_data( infile, &oy );
            while ( ogg_sync_pageout( &oy, &og ) > 0 )
            {
                queue_page( &og );
            }
        }

        /* are we at or past time for this video frame? */
        if ( stateflag && videobuf_ready && videobuf_time <= get_time() )
        {
            video_write();
            videobuf_ready = 0;
        }

        if ( stateflag && ( videobuf_ready || !theora_p ) && !got_sigint )
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

    /* tear it all down */

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

    return ( 0 );
}

namespace theoraplayer
{
    void test()
    {
        test2();
    }
}
