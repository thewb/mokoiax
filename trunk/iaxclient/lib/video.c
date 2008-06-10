/*
 * iaxclient: a cross-platform IAX softphone library
 *
 * Copyrights:
 * Copyright (C) 2005-2006, Horizon Wimba, inc.
 * Copyright (C) 2007, Wimba, Inc.
 *
 * Contributors:
 * Steve Kann <stevek@stevek.com>
 * Mihai Balea <mihai AT hates DOT ms>
 * Peter Grayson <jpgrayson@gmail.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU Lesser (Library) General Public License.
 */

#include <assert.h>

#include "video.h"
#include "iaxclient_lib.h"
#include "videoLib/video_grab.h"
#include "iax-client.h"
#ifdef USE_FFMPEG
#include "codec_ffmpeg.h"
#endif
#include "codec_theora.h"

#define VIDEO_BUFSIZ (1<<19)

extern int selected_call;
extern struct iaxc_call * calls;

static int iaxc_video_width = 320;
static int iaxc_video_height = 240;
static int iaxc_video_framerate = 10; //15;
static int iaxc_video_bitrate = 150000;
static int iaxc_video_fragsize = 1500;
static int iaxc_video_format_preferred = 0;
static int iaxc_video_format_allowed = 0;
static struct iaxc_video_driver video_driver;

/* Set the default so that the local and remote raw video is
 * sent to the client application and encoded video is sent out.
 */
static int iaxc_video_prefs = IAXC_VIDEO_PREF_RECV_LOCAL_RAW |
                              IAXC_VIDEO_PREF_RECV_REMOTE_RAW;

#if 0
/* debug: check a yuv420p buffer to ensure it's within the CCIR range */
static int check_ccir_yuv(char *data) {
    int i;
    unsigned char pix;
    int err = 0;

    for(i=0;i<iaxc_video_width * iaxc_video_height; i++) {
	pix = *data++;
	if( (pix < 16) || pix > 235) {
	    fprintf(stderr, "check_ccir_yuv: Y pixel[%d] out of range: %d\n", i, pix);
	    err++;
	}
    }
    for(i=0;i< iaxc_video_width * iaxc_video_height / 2; i++) {
	pix = *data++;
	if( (pix < 16) || pix > 239) {
	    fprintf(stderr, "check_ccir_yuv: U/V pixel[%d] out of range: %d\n", i, pix);
	    err++;
	}
    }
    return err;
}
#endif

EXPORT unsigned int iaxc_get_video_prefs(void)
{
	return iaxc_video_prefs;
}

EXPORT int iaxc_set_video_prefs(unsigned int prefs)
{
	const unsigned int prefs_mask =
		IAXC_VIDEO_PREF_RECV_LOCAL_RAW      |
		IAXC_VIDEO_PREF_RECV_LOCAL_ENCODED  |
		IAXC_VIDEO_PREF_RECV_REMOTE_RAW     |
		IAXC_VIDEO_PREF_RECV_REMOTE_ENCODED |
		IAXC_VIDEO_PREF_SEND_DISABLE        |
		IAXC_VIDEO_PREF_RECV_RGB32          |
		IAXC_VIDEO_PREF_CAPTURE_DISABLE;

	if ( prefs & ~prefs_mask )
		return -1;

	/* Not sending any video and not needing any form of
	 * local video implies that we do not need to capture
	 * video.
	 */
	if ( prefs & IAXC_VIDEO_PREF_CAPTURE_DISABLE ||
			((prefs & IAXC_VIDEO_PREF_SEND_DISABLE) &&
			 !(prefs & IAXC_VIDEO_PREF_RECV_LOCAL_RAW) &&
			 !(prefs & IAXC_VIDEO_PREF_RECV_LOCAL_ENCODED)) )
	{
		/* Note that in both the start and stop cases, we
		 * rely on the start/stop function to be idempotent.
		 */
		if (video_driver.stop)
			video_driver.stop(&video_driver);
	}
	else
	{
		if ( video_driver.start )
		{
			video_driver.start(&video_driver);

			// Driver may fail to start
			if ( !video_driver.is_camera_working(&video_driver) )
				return -1;
		}
	}

	iaxc_video_prefs = prefs;

	return 0;
}

EXPORT void iaxc_video_format_set_cap(int preferred, int allowed)
{
	iaxc_video_format_preferred = preferred;
	iaxc_video_format_allowed = allowed;
}

EXPORT void iaxc_video_format_get_cap(int *preferred, int *allowed)
{
	*preferred = iaxc_video_format_preferred;
	*allowed = iaxc_video_format_allowed;
}

EXPORT void iaxc_video_format_set(int preferred, int allowed, int framerate,
		int bitrate, int width, int height, int fs)
{
	int real_pref = 0;
	int real_allowed = 0;
#ifdef USE_FFMPEG
	int tmp_allowed;
	int i;
#endif

	// Make sure resolution is in range
	if ( width < IAXC_VIDEO_MIN_WIDTH )
		width = IAXC_VIDEO_MIN_WIDTH;
	else if ( width > IAXC_VIDEO_MAX_WIDTH )
		width = IAXC_VIDEO_MAX_WIDTH;

	if ( height < IAXC_VIDEO_MIN_HEIGHT )
		height = IAXC_VIDEO_MIN_HEIGHT;
	else if ( height > IAXC_VIDEO_MAX_HEIGHT )
		height = IAXC_VIDEO_MAX_HEIGHT;

	iaxc_video_framerate = framerate;
	iaxc_video_bitrate = bitrate;
	iaxc_video_width = width;
	iaxc_video_height = height;
	iaxc_video_fragsize = fs;

	iaxc_video_format_allowed = 0;
	iaxc_video_format_preferred = 0;

	if ( preferred && (preferred & ~IAXC_VIDEO_FORMAT_MASK) )
	{
		fprintf(stderr, "ERROR: Preferred video format invalid.\n");
		preferred = 0;
	}

	/* This check:
	 * 1. Check if preferred is a supported and compiled in codec. If
	 *    not, switch to the default preferred format.
	 * 2. Check if allowed contains a list of all supported and compiled
	 *    in codec. If there are some unavailable codec, remove it from
	 *    this list.
	 */

	if ( preferred & IAXC_FORMAT_THEORA )
		real_pref = IAXC_FORMAT_THEORA;

#ifdef USE_FFMPEG
	if ( codec_video_ffmpeg_check_codec(preferred) )
		real_pref = preferred;
#endif

#ifdef USE_H264_VSS
	if ( preferred & IAXC_FORMAT_H264 )
		real_pref = IAXC_FORMAT_H264;
#endif

	if ( !real_pref )
	{
		// If preferred codec is not available switch to the
		// supported default codec.
		fprintf(stderr, "Preferred codec (0x%08x) is not available. Switching to default", preferred);
		real_pref = IAXC_FORMAT_THEORA;
	}

	/* Check on allowed codecs availability */

	if ( allowed & IAXC_FORMAT_THEORA )
		real_allowed |= IAXC_FORMAT_THEORA;

#ifdef USE_FFMPEG
	/* TODO: This codec_video_ffmpeg_check_codec stuff is bogus. We
	 * need a standard interface in our codec wrappers that allows us to
	 * (1) test if a selected codec is valid and/or (2) return the set of
	 * available valid codecs. With that, we should be able to come up
	 * with a more elegant algorithm here for determining the video codec.
	 */
	for ( i = 0; i <= 24; i++)
	{
		tmp_allowed = 1 << i;
		if ( (allowed & tmp_allowed)  &&
				 codec_video_ffmpeg_check_codec(tmp_allowed) )
			real_allowed |= tmp_allowed;
	}
#endif

	if ( !real_pref )
	{
		fprintf(stderr, "Audio-only client!\n");
	} else
	{
		iaxc_video_format_preferred = real_pref;

		/*
		 * When a client use a 'preferred' format, it can force to
		 * use allowed formats using a non-zero value for 'allowed'
		 * parameter. If it is left 0, the client will use all
		 * capabilities set by default in this code.
		 */
		if ( real_allowed )
		{
			iaxc_video_format_allowed = real_allowed;
		} else
		{
#ifdef USE_FFMPEG
			iaxc_video_format_allowed |= IAXC_FORMAT_H263_PLUS
				| IAXC_FORMAT_H263
				| IAXC_FORMAT_MPEG4
				| IAXC_FORMAT_H264;
#endif
			iaxc_video_format_allowed |= IAXC_FORMAT_THEORA;
		}
	}
}

void iaxc_video_params_change(int framerate, int bitrate, int width,
		int height, int fs)
{
	struct iaxc_call *call;

	/* set default video params */
	if ( framerate > 0 )
		iaxc_video_framerate = framerate;
	if ( bitrate > 0 )
		iaxc_video_bitrate = bitrate;
	if ( width > 0 )
		iaxc_video_width = width;
	if ( height > 0 )
		iaxc_video_height = height;
	if ( fs > 0 )
		iaxc_video_fragsize = fs;

	if ( selected_call < 0 )
		return;

	call = &calls[selected_call];

	if ( !call || !call->vencoder )
		return;

	call->vencoder->params_changed = 1;

	if ( framerate > 0 )
		call->vencoder->framerate = framerate;
	if ( bitrate > 0 )
		call->vencoder->bitrate = bitrate;
	if ( width > 0 )
		call->vencoder->width = width;
	if ( height > 0 )
		call->vencoder->height = height;
	if ( fs > 0 )
		call->vencoder->fragsize = fs;
}

/* TODO: The encode parameter to this function is unused within this
 * function. However, clients of this function still use this parameter.
 * What ends up happening is we instantiate the codec encoder/decoder
 * pairs multiple times. This seems not the best. For example, it is
 * not clear that all codecs are idempotent to the extent that they can
 * be initialized multiple times. Furthermore, within iaxclient itself,
 * we have a nasty habit of using global statically allocated data.
 * Multiple codec_video_*_new calls could easily result in race
 * conditions or more blatantly bad problems.
 *
 * Splitting encoder and decoder creation has some merit.
 *
 *  - Avoid allocating encoder or decoder resources when not needed.
 *  - Allows using different codecs for sending and receiving.
 *  - Allows different codec parameters for sending and receiving.
 *
 */
static struct iaxc_video_codec *create_codec(int format, int encode)
{
	iaxci_usermsg(IAXC_TEXT_TYPE_NOTICE, "Creating codec format 0x%x",
			format);
	switch ( format )
	{
	case IAXC_FORMAT_H261:
	case IAXC_FORMAT_H263:
	case IAXC_FORMAT_H263_PLUS:
	case IAXC_FORMAT_MPEG4:
#ifdef USE_FFMPEG
		return codec_video_ffmpeg_new(format,
				iaxc_video_width,
				iaxc_video_height,
				iaxc_video_framerate,
				iaxc_video_bitrate,
				iaxc_video_fragsize);
#else
		return NULL;
#endif

	case IAXC_FORMAT_H264:
#ifdef USE_FFMPEG
		return codec_video_ffmpeg_new(format,
				iaxc_video_width,
				iaxc_video_height,
				iaxc_video_framerate,
				iaxc_video_bitrate,
				iaxc_video_fragsize);
#else
		return NULL;
#endif

	case IAXC_FORMAT_THEORA:
		return NULL;
/*		return codec_video_theora_new(format,
				iaxc_video_width,
				iaxc_video_height,
				iaxc_video_framerate,
				iaxc_video_bitrate,
				iaxc_video_fragsize);
*/
	}

	// Must never happen...
	return NULL;
}

/*
 * show_video_frame - returns video data to the main application
 * using the callback mechanism
 * This function creates a dynamic copy of the video data.  The memory is freed
 * in iaxci_post_event. This is because the event we post may be queued and the
 * frame data must live until after it is dequeued.
 * Parameters: - videobuf: buffer containing raw or encoded video data
 *             - size - size of video data block
 *             - cn - call number
 *             - source - either IAXC_SOURCE_LOCAL or IAXC_SOURCE_REMOTE
 *             - encoded - true if data is encoded
 *             - rgb32 - if true, convert data to RGB32 before showing
 */
void show_video_frame(char *videobuf, int size, int cn, int source, int encoded,
		unsigned int ts, int rgb32)
{
	iaxc_event e;
	char * buffer;

	e.type = IAXC_EVENT_VIDEO;
	e.ev.video.ts = ts;

	if ( size <= 0 )
		fprintf(stderr, "WARNING: size %d in show_video_frame\n", size);

	if ( !encoded && rgb32 )
	{
		e.ev.video.size = iaxc_video_height * iaxc_video_width * 4;
		buffer = (char *)malloc(e.ev.video.size);
		assert(buffer);
		e.ev.video.data = buffer;
		iaxc_YUV420_to_RGB32(iaxc_video_width, iaxc_video_height,
				videobuf, buffer);
	} else
	{
		buffer = (char *)malloc(size);
		assert(buffer);
		memcpy(buffer, videobuf, size);
		e.ev.video.data = buffer;
		e.ev.video.size = size;
	}

	e.ev.video.format = iaxc_video_format_preferred;
	e.ev.video.width = iaxc_video_width;
	e.ev.video.height = iaxc_video_height;
	e.ev.video.callNo = cn;
	e.ev.video.encoded = encoded;
	assert(source == IAXC_SOURCE_REMOTE || source == IAXC_SOURCE_LOCAL);
	e.ev.video.source = source;

	iaxci_post_event(e);
}

/* try to get the next frame, encode and send */
int video_send_video(struct iaxc_call *call, int sel_call)
{
	static struct slice_set_t slice_set;
	int format;
	int i = 0;
	const int inlen = iaxc_video_width * iaxc_video_height * 6 / 4;
	char * videobuf;
	struct timeval now;
	long time;

	video_driver.input(&video_driver, &videobuf);

	/* It is okay if we do not get any video; video capture may be
	 * disabled.
	 */
	if ( !videobuf ||
	     (iaxc_video_prefs & IAXC_VIDEO_PREF_CAPTURE_DISABLE)
	   )
		return 0;

	// Send the raw frame to the main app, if necessary
	if ( iaxc_video_prefs & IAXC_VIDEO_PREF_RECV_LOCAL_RAW )
	{
		show_video_frame(videobuf, inlen, -1, IAXC_SOURCE_LOCAL, 0, 0,
				iaxc_video_prefs & IAXC_VIDEO_PREF_RECV_RGB32);
	}

	if ( sel_call < 0 || !call || !(call->state & (IAXC_CALL_STATE_COMPLETE | IAXC_CALL_STATE_OUTGOING) ) )
	{
		return -1;
	}

	// use the calls format, not random preference
	format = call->vformat;

	if ( format == 0 )
	{
//		fprintf(stderr, "video_send_video: Format is zero (should't happen)!\n");
		return -1;
	}

	// If we don't need to send encoded video to the network or back
	// to the main application, just return here.
	if ( !(iaxc_video_prefs & IAXC_VIDEO_PREF_RECV_LOCAL_ENCODED) &&
	     (iaxc_video_prefs & IAXC_VIDEO_PREF_SEND_DISABLE) )
	{
		if ( call->vencoder )
		{
			// We don't need to encode video so just destroy the encoder
			fprintf(stderr, "Destroying codec %s\n", call->vencoder->name);
			call->vencoder->destroy(call->vencoder);
			call->vencoder = NULL;
		}
		return 0;
	}else
	{
		/* destroy vencoder if it is incorrect type */
		if ( call->vencoder &&
		     (call->vencoder->format != format || call->vencoder->params_changed)
		   )
		{
			call->vencoder->destroy(call->vencoder);
			call->vencoder = NULL;
		}

		/* create encoder if necessary */
		if ( !call->vencoder )
		{
			call->vencoder = create_codec(format, 1);
			fprintf(stderr,"**** Created encoder codec %s\n",call->vencoder->name);
		}

		if ( !call->vencoder )
		{
			fprintf(stderr,
				"ERROR: Video codec could not be created: 0x%08x\n",
				format);
			return -1;
		}

		// encode the frame
		if ( call->vencoder->encode(call->vencoder, inlen, videobuf,
					&slice_set) )
		{
			fprintf(stderr, "video_send_video: encode failed\n");
			return -1;
		}
	}

	// Statistics
	gettimeofday(&now, NULL);
	call->vencoder->video_stats.outbound_frames++;
	time = iaxci_msecdiff(&now, &call->vencoder->video_stats.start_time);
	if ( time > 0 )
		call->vencoder->video_stats.avg_outbound_fps =
			(float)call->vencoder->video_stats.outbound_frames *
			1000 / time;

	// send the frame!

	if ( !call->session )
		return -1;

	for ( i = 0; i < slice_set.num_slices; i++ )
	{
		//Pass the encoded frame to the main app
		// TODO: fix the call number
		if ( iaxc_video_prefs & IAXC_VIDEO_PREF_RECV_LOCAL_ENCODED )
		{
			show_video_frame(slice_set.data[i], slice_set.size[i],
					-1, IAXC_SOURCE_LOCAL, 1, 0, 0);
		}

		if ( !(iaxc_video_prefs & IAXC_VIDEO_PREF_SEND_DISABLE) )
		{
			if ( iax_send_video_trunk(call->session, format,
						slice_set.data[i],
						slice_set.size[i],
						0, i) == -1 )
			{
				fprintf(stderr, "Failed to send a slice, call %d, size %d\n",
						sel_call, slice_set.size[i]);
				return -1;
			}

			// Statistics
			call->vencoder->video_stats.sent_slices++;
			call->vencoder->video_stats.acc_sent_size +=
				slice_set.size[i];
			if ( time > 0 )
				call->vencoder->video_stats.avg_outbound_bps =
					call->vencoder->video_stats.acc_sent_size *
					8000 / time;
		}
	}

	return 0;
}

/* process an incoming video frame */
int video_recv_video(struct iaxc_call *call, int sel_call,
		void *encoded_video, int encoded_video_len,
		unsigned int ts, int format)
{
	static char videobuf[VIDEO_BUFSIZ];
	int outsize = VIDEO_BUFSIZ;
	int ret_dec;
	struct timeval now;
	long time;

	if ( !call )
		return 0;

	if ( format == 0 )
	{
		fprintf(stderr, "video_recv_video: Format is zero (should't happen)!\n");
		return -1;
	}

	// Send the encoded frame to the main app if necessary
	if ( iaxc_video_prefs & IAXC_VIDEO_PREF_RECV_REMOTE_ENCODED )
	{
		show_video_frame((char *)encoded_video, encoded_video_len, -1,
				IAXC_SOURCE_REMOTE, 1, ts, 0);
	}

	/* destroy vdecoder if it is incorrect type */
	if ( call->vdecoder && call->vdecoder->format != format )
	{
		call->vdecoder->destroy(call->vdecoder);
		call->vdecoder = NULL;
	}

	/* If the user does not need decoded video, then do not decode. */
	if ( !(iaxc_video_prefs & IAXC_VIDEO_PREF_RECV_REMOTE_RAW) )
		return 0;

	/* create decoder if necessary */
	if ( !call->vdecoder )
	{
		call->vdecoder = create_codec(format, 0);
		fprintf(stderr,"**** Created decoder codec %s\n",call->vdecoder->name);
	}

	if ( !call->vdecoder )
	{
		fprintf(stderr, "ERROR: Video codec could not be created: %d\n",
				format);
		return -1;
	}

	/* Statistics */
	gettimeofday(&now, NULL);
	time = iaxci_msecdiff(&now, &call->vdecoder->video_stats.start_time);
	call->vdecoder->video_stats.received_slices++;
	call->vdecoder->video_stats.acc_recv_size += encoded_video_len;
	if ( time > 0 )
		call->vdecoder->video_stats.avg_inbound_bps =
			call->vdecoder->video_stats.acc_recv_size * 8000 / time;

	ret_dec = call->vdecoder->decode(call->vdecoder, encoded_video_len,
			(char *)encoded_video, &outsize, videobuf);

	if ( ret_dec < 0 )
	{
		fprintf(stderr, "ERROR: decode error\n");
		return -1;
	}
	else if ( ret_dec > 0 )
	{
		/* This indicates that a complete frame cannot yet
		 * be decoded. This is okay.
		 */
		return 0;
	}

	/* Statistics */
	call->vdecoder->video_stats.inbound_frames++;
	if ( time > 0 )
		call->vdecoder->video_stats.avg_inbound_fps =
			call->vdecoder->video_stats.inbound_frames *
			1000.0F / time;

	if ( outsize > 0 )
	{
		show_video_frame(videobuf, outsize, sel_call,
				IAXC_SOURCE_REMOTE, 0, ts,
				iaxc_video_prefs & IAXC_VIDEO_PREF_RECV_RGB32);
	}

	return 0;
}

int video_initialize(void)
{
	if ( pv_initialize(&video_driver, iaxc_video_width, iaxc_video_height,
			iaxc_video_framerate) )
	{
		fprintf(stderr, "ERROR: cannot initialize pv\n");
		return -1;
	}

	/* We reset the existing video preferences to yield the side-effect
	 * of potentially starting or stopping the video capture.
	 */
	iaxc_set_video_prefs(iaxc_video_prefs);

	return 0;
}

int video_destroy(void)
{
	if ( video_driver.destroy )
		return video_driver.destroy(&video_driver);
	else
		return -1;
}

/*
 * YUV to RGB conversion
 */

#define CLIP_SIZE   811
#define CLIP_OFFSET 277

#define YMUL  298
#define RMUL  409
#define BMUL  516
#define G1MUL -100
#define G2MUL -208

static int           yuv2rgb_y[256];
static int           yuv2rgb_r[256];
static int           yuv2rgb_b[256];
static int           yuv2rgb_g1[256];
static int           yuv2rgb_g2[256];
static unsigned long yuv2rgb_clip[CLIP_SIZE];
static unsigned long yuv2rgb_clip8[CLIP_SIZE];
static unsigned long yuv2rgb_clip16[CLIP_SIZE];
static int           yuv2rgb_tables_initialized = 0;

#define RED(y, v)     yuv2rgb_clip16[CLIP_OFFSET + yuv2rgb_y[y] + yuv2rgb_r[v]]
#define GREEN(y,v,u)  yuv2rgb_clip8[CLIP_OFFSET + yuv2rgb_y[y] + yuv2rgb_g1[v] + yuv2rgb_g2[u]]
#define BLUE(y,u)     yuv2rgb_clip[CLIP_OFFSET + yuv2rgb_y[y] + yuv2rgb_b[u]]

static void iaxc_init_yuv2rgb_tables(void)
{
	int i;

	for (i = 0; i < 256; i++)
	{
		yuv2rgb_y[i] = (YMUL * (i - 16) + 128) >> 8;
		yuv2rgb_r[i] = (RMUL * (i - 128)) >> 8;
		yuv2rgb_b[i] = (BMUL * (i - 128)) >> 8;
		yuv2rgb_g1[i] = (G1MUL * (i - 128)) >> 8;
		yuv2rgb_g2[i] = (G2MUL * (i - 128)) >> 8;
	}
	for ( i = 0 ; i < CLIP_OFFSET ; i++ )
	{
		yuv2rgb_clip[i] = 0;
		yuv2rgb_clip8[i] = 0;
		yuv2rgb_clip16[i] = 0;
	}
	for ( ; i < CLIP_OFFSET + 256 ; i++ )
	{
		yuv2rgb_clip[i] = i - CLIP_OFFSET;
		yuv2rgb_clip8[i] = (i - CLIP_OFFSET) << 8;
		yuv2rgb_clip16[i] = (i - CLIP_OFFSET) << 16;
	}
	for ( ; i < CLIP_SIZE ; i++ )
	{
		yuv2rgb_clip[i] = 255;
		yuv2rgb_clip8[i] = 255 << 8;
		yuv2rgb_clip16[i] = 255 << 16;
	}

	yuv2rgb_tables_initialized = 1;
}


/*
 * Faster function to convert YUV420 images to RGB32
 * RGB32: 0xFFRRGGBB
 * This function uses precalculated tables that are initialized
 * on the first run.
 * Make sure the src and dest buffers have enough room
 * dest should be width * height * 4 bytes in size
 * Based on the formulas found at http://en.wikipedia.org/wiki/YUV
 */
void iaxc_YUV420_to_RGB32(int width, int height, char *src, char *dest)
{
	unsigned char *y, *u, *v;
	unsigned int *dst;
	int i;

	if ( !yuv2rgb_tables_initialized )
		iaxc_init_yuv2rgb_tables();

	dst = (unsigned int *)dest;
	y  = (unsigned char *)src;
	u  = y + width * height;
	v  = u + width * height / 4;

	for (i = 0; i < height; i++)
	{
		unsigned char *uu,*vv;
		unsigned int *d;
		int j;

		d = dst;
		uu = u;
		vv = v;
		for ( j = 0; j < width >> 1; j++ )
		{
			*(d++) = 0xff000000 |
			         RED(*y,*vv) |
			         GREEN(*y,*vv,*uu) |
			         BLUE(*y,*uu);
			y++;
			*(d++) = 0xff000000 |
			         RED(*y,*vv) |
			         GREEN(*y,*vv,*uu) |
			         BLUE(*y,*uu);
			y++;
			uu++;
			vv++;
		}
		if ( i & 1 )
		{
			u += width >> 1;
			v += width >> 1;
		}
		dst += width;
	}
}

/*
 * Original function that converts YUV420 images to RGB32
 * RGB32: 0xFFRRGGBB
 * Make sure the src and dest buffers have enough room
 * dest should be width * height * 4 bytes in size
 * Based on the formulas found at http://en.wikipedia.org/wiki/YUV
 */
// void iaxc_YUV420_to_RGB32(int width, int height, char *src, char *dest)
// {
// 	int i;
// 	unsigned char * y = (unsigned char *)src;
// 	unsigned char * u = y + width * height;
// 	unsigned char * v = u + width * height / 4;
// 	unsigned int * dst = (unsigned int *)dest;
//
// 	for ( i = 0; i < height; i++ )
// 	{
// 		int j;
//
// 		unsigned char * uu = u;
// 		unsigned char * vv = v;
//
// 		for ( j = 0; j < width; j++ )
// 		{
// 			int yyy =  *y -  16;
// 			int uuu = *uu - 128;
// 			int vvv = *vv - 128;
//
// 			int r = ( 298*yyy + 409*vvv + 128) >> 8;
// 			int g = ( 298*yyy - 100*uuu - 208*vvv + 128) >> 8;
// 			int b = ( 298*yyy + 516*uuu + 128) >> 8;
//
// 			// Clip values to make sure they fit in range
// 			if ( r < 0 )
// 				r = 0;
// 			else if ( r > 255 )
// 				r = 255;
//
// 			if ( g < 0 )
// 				g = 0;
// 			else if ( g > 255 )
// 				g = 255;
//
// 			if ( b < 0 )
// 				b = 0;
// 			else if ( b > 255 )
// 				b = 255;
//
// 			*(dst++) = 0xff000000 |
// 				((unsigned char)r << 16) |
// 				((unsigned char)g << 8) |
// 				((unsigned char)b << 0);
//
// 			y++;
//
// 			if ( j & 1 )
// 			{
// 				uu++;
// 				vv++;
// 			}
// 		}
//
// 		if ( i & 1 )
// 		{
// 			u += width >> 1;
// 			v += width >> 1;
// 		}
// 	}
// }

int iaxc_is_camera_working()
{
	return video_driver.is_camera_working(&video_driver);
}

static void reset_video_stats(struct iaxc_call *call)
{
	if ( !call )
		return;

	video_reset_codec_stats(call->vdecoder);
	video_reset_codec_stats(call->vencoder);
}

// Collect and return video statistics
// Also reset statistics if required;
// Returns a pointer to the data
// Right now we use two different codecs for encoding and decoding. We need to collate information
// from both and wrap it into one nice struct
int video_get_stats(struct iaxc_call *call, struct iaxc_video_stats *stats,
		int reset)
{
	if ( !call || !stats )
		return -1;

	memset(stats, 0, sizeof(*stats));

	if ( call->vencoder != NULL )
	{
		stats->sent_slices = call->vencoder->video_stats.sent_slices;
		stats->acc_sent_size = call->vencoder->video_stats.acc_sent_size;
		stats->outbound_frames = call->vencoder->video_stats.outbound_frames;
		stats->avg_outbound_bps = call->vencoder->video_stats.avg_outbound_bps;
		stats->avg_outbound_fps = call->vencoder->video_stats.avg_outbound_fps;
	}

	if ( call->vdecoder != NULL )
	{
		stats->received_slices = call->vdecoder->video_stats.received_slices;
		stats->acc_recv_size = call->vdecoder->video_stats.acc_recv_size;
		stats->inbound_frames = call->vdecoder->video_stats.inbound_frames;
		stats->dropped_frames = call->vdecoder->video_stats.dropped_frames;
		stats->avg_inbound_bps = call->vdecoder->video_stats.avg_inbound_bps;
		stats->avg_inbound_fps = call->vdecoder->video_stats.avg_inbound_fps;
	}

	if ( reset )
		reset_video_stats(call);

	return 0;
}

void video_reset_codec_stats(struct iaxc_video_codec *vcodec)
{
	if ( vcodec == NULL ) return;

	memset(&vcodec->video_stats, 0, sizeof(struct iaxc_video_stats));
	gettimeofday(&vcodec->video_stats.start_time, NULL);
}

