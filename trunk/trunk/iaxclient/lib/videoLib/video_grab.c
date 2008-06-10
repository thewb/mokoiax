/*
 * iaxclient: a cross-platform IAX softphone library
 *
 * Copyrights:
 * Copyright (C) 2006, Horizon Wimba, Inc.
 * Copyright (C) 2007, Wimba, Inc.
 *
 * Contributors:
 * Steve Kann <stevek@stevek.com>
 * Stefano Falsetto <falsetto@gnu.org>
 * Francesco Delfino <pluto@tipic.com>
 * Mihai Balea <mihai AT hates DOT ms>
 * Peter Grayson <jpgrayson@gmail.com>
 * Bill Cholewka <bcholew@gmail.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU Lesser (Library) General Public License.
 */

/*
 * Module: video_grab
 * Purpose: Video code to provide portvideo driver support for IAX library
 * Developed by: Stefano Falsetto and Francesco Delfino
 * Creation Date: November, 28, 2005
 */

#include "iaxclient_lib.h"
#include "video.h"
#include "video_grab.h"

static int img_buffer_full;
static int img_buffer_size = 0;
static char * img_buffer = 0;
static char * resized_img_buffer = 0;
static char * converted_img_buffer = 0;
static void * grabber = 0;

static int framerate;
static struct timeval next_frame_time;
static int width, height;

static MUTEX mytst;
static MUTEX vdriverMutex;

static int send_waiting_frame = 1;
static char * waiting_frame = 0;

#if defined(LINUX) || defined(MACOSX)
  #define min(x,y) ((x)<(y)?(x):(y))
#endif

static int my_test_and_set(int *what, int newval, int check)
{
	long retval;

	MUTEXLOCK(&mytst);
	retval = *what;
	if ( *what == check)
		*what = newval;
	MUTEXUNLOCK(&mytst);

	return retval;
}

// Resize the channel to 25% (half resolution on both w and h )
// No checks are made to ensure that the source dimensions are even numbers
static void pv_quarter_channel(const unsigned char *src, int src_w, int src_h,
		unsigned char *dst)
{
	int i;
	const unsigned char * evenl = src;
	const unsigned char * oddl = src + src_w;

	for ( i = 0 ; i < src_h / 2 ; i++ )
	{
		int j;
		for ( j = 0 ; j < src_w / 2 ; j++ )
		{
			int s = *(evenl++);
			s += *(evenl++);
			s += *(oddl++);
			s += *(oddl++);
			*(dst++) = (unsigned char)(s >> 2);
		}
		evenl = oddl;
		oddl += src_w;
	}
}

// Resize the channel to 1/16 size (quarter resolution on both w and h )
// NO checks are made to ensure that source dimensions are multiple of 4
static void pv_double_quarter_channel(const unsigned char *src, int src_w,
		int src_h, unsigned char *dst)
{
	int i;
	const unsigned char * line0 = src;
	const unsigned char * line1 = line0 + src_w;
	const unsigned char * line2 = line1 + src_w;
	const unsigned char * line3 = line2 + src_w;

	for ( i = 0; i < src_h / 4 ; i++ )
	{
		int j;
		for ( j = 0 ; j < src_w / 4 ; j++ )
		{
			int s;

			s = *(line0++);
			s += *(line0++);
			s += *(line0++);
			s += *(line0++);

			s += *(line1++);
			s += *(line1++);
			s += *(line1++);
			s += *(line1++);

			s += *(line2++);
			s += *(line2++);
			s += *(line2++);
			s += *(line2++);

			s += *(line3++);
			s += *(line3++);
			s += *(line3++);
			s += *(line3++);

			*(dst++) = (unsigned char)(s >> 4);
		}

		line0 = line3;
		line1 = line0 + src_w;
		line2 = line1 + src_w;
		line3 = line2 + src_w;
	}
}

// Resize one channel using the nearest neighbour method
// The destination buffer has to be large enough for the resulting image
static void pv_resize_channel(const unsigned char * src, int src_w, int src_h,
		unsigned char * dst, int dst_w, int dst_h)
{
	int i, j, s, d;
	float h_stride, w_stride, x, y;

	h_stride = (float)src_h / (float)dst_h;
	w_stride = (float)src_w / (float)dst_w;

	y = 0;
	s = 0;
	d = 0;

	for ( i = 0; i < dst_h; i++ )
	{
		x = 0;
		for ( j = 0; j < dst_w; j++ )
		{
			dst[d++] = src[s+(int)x];
			x += w_stride;
		}
		y += h_stride;
		s = src_w * (int)y;
	}
}

// Resize a YUV420 image by calling pv_resize_channel for each of the three
// channels. Destination buffer must be sufficiently large to accommodate
// the resulting image
void pv_resize_yuv420(const unsigned char *src, int src_w, int src_h,
		unsigned char *dst, int dst_w, int dst_h)
{
	const unsigned char *src_u = src + src_w * src_h;
	const unsigned char *src_v = src_u + src_w * src_h / 4;
	unsigned char *dst_u = dst + dst_w * dst_h;
	unsigned char *dst_v = dst_u + dst_w * dst_h / 4;

	// Resize each channel separately
	// Try to use specialized functions if possible
	if ( dst_w * 2 == src_w && dst_h * 2 == src_h )
	{
		pv_quarter_channel(src, src_w, src_h, dst);
		pv_quarter_channel(src_u, src_w / 2, src_h / 2, dst_u);
		pv_quarter_channel(src_v, src_w / 2, src_h / 2, dst_v);
	}
	else if ( dst_w * 4 == src_w && dst_h * 4 == src_h )
	{
		pv_double_quarter_channel(src, src_w, src_h, dst);
		pv_double_quarter_channel(src_u, src_w / 2, src_h / 2, dst_u);
		pv_double_quarter_channel(src_v, src_w / 2, src_h / 2, dst_v);
	}
	else
	{
		pv_resize_channel(src, src_w, src_h, dst, dst_w, dst_h);
		pv_resize_channel(src_u, src_w / 2, src_h / 2,
				dst_u, dst_w / 2, dst_h / 2);
		pv_resize_channel(src_v, src_w / 2, src_h / 2,
				dst_v, dst_w / 2, dst_h / 2);
	}
}

static void increment_next_frame_time()
{
	const long million = 1000000L;
	const long new_usec = next_frame_time.tv_usec +
		million / framerate;

	next_frame_time.tv_sec += new_usec / million;
	next_frame_time.tv_usec = new_usec % million;
}

static void my_grabber_callback( void *gc_data, double sample_time,
		void * in_buf, long in_buf_size)
{
	if ( !in_buf )
		return;

	if ( !my_test_and_set(&img_buffer_full, 1, 0) )
	{
		// YUY2 has more data than I420/IYUV
		if ( grabber_format_check(IMAGE_FORMAT_YUY2) )
			in_buf_size += in_buf_size / 3;

		memcpy(img_buffer, in_buf, min(in_buf_size, img_buffer_size));
	}
}

int pv_start(struct iaxc_video_driver *d )
{
	MUTEXLOCK(&vdriverMutex);
	if ( grabber )
	{
		MUTEXUNLOCK(&vdriverMutex);
		return 0;
	}

	img_buffer_full = 0;

	if ( !(grabber = grabber_init(my_grabber_callback, NULL, width, height)) )
	{
		send_waiting_frame = 0;
		d->camera_working = 0;
	} else
	{
		d->camera_working = 1;
	}

	gettimeofday(&next_frame_time, NULL);
	increment_next_frame_time();

	MUTEXUNLOCK(&vdriverMutex);
	return 0;
}

int pv_stop(struct iaxc_video_driver *d )
{
	MUTEXLOCK(&vdriverMutex);
	if ( grabber )
	{
		grabber_destroy(grabber);
		grabber = 0;
		img_buffer_full = 0;
		send_waiting_frame = 1;
	}

	MUTEXUNLOCK(&vdriverMutex);
	return 0;
}

void yuy2_to_yuv420(const unsigned char *src, int width, int height,
		unsigned char *dst)
{
	// convert from a packed structure
	// to a planar structure
	unsigned char *dst_y = dst;
	unsigned char *dst_u = dst + width * height;
	unsigned char *dst_v = dst_u + width * height / 4;

	int y = 0;
	int u = 0;
	int v = 0;
	int s = 0;
	int i, j;

	// YUY2 has a vertical sampling PERIOD (for U and V)
	// half that for YUV420. Will toss half of the
	// U and V data during repackaging.
	for ( i = 0; i < height / 2; i++ )
	{
		for ( j = 0; j < width / 2; j++ )
		{
			//even line  Y0
			dst_y[y]       = src[s];

			//odd line  Y0
			dst_y[y + width] = src[s + width * 2];
			y++;
			s++;

			//both lines  U0
			dst_u[u++]     = src[s++];

			//even line  Y1
			dst_y[y]       = src[s];

			//odd line  Y1
			dst_y[y + width] = src[s + width * 2];
			y++;
			s++;

			//both lines  V0
			dst_v[v++]     = src[s++];

			//odd lines - skip u and v data
		}
		// skip one line
		s += 2 * width;
		y += width;
	}
}

int pv_input(struct iaxc_video_driver *d, char **in)
{
	static int consec_null_frames = 0;

	char *frame = NULL;
	struct timeval current;

	gettimeofday(&current, NULL);

	if ( iaxci_usecdiff(&current, &next_frame_time) < 0 )
	{
		*in = NULL;
		return 0;
	}

	increment_next_frame_time();

	if ( my_test_and_set(&img_buffer_full, 0, 1) )
	{
		frame = img_buffer;
		send_waiting_frame = 0;
		consec_null_frames = 0;
	} else
	{
		frame = NULL;
		consec_null_frames++;
	}

	if ( frame != NULL && grabber_format_check(IMAGE_FORMAT_YUY2) )
	{
		// convert from YUY2 to YUV420
		yuy2_to_yuv420((const unsigned char *)frame, width, height,
			(unsigned char *)converted_img_buffer);
		frame = converted_img_buffer;
	}

	if ( consec_null_frames && (consec_null_frames % (5 * framerate) == 0) )
	{
		// I assume that connection with camera has some problem, so
		// I will send waiting_frame
		send_waiting_frame = 1;
		consec_null_frames = 0;
	}

	if ( !frame && send_waiting_frame )
	{
		frame = waiting_frame;
	}

	if ( d->needs_resize && frame != NULL )
	{
		pv_resize_yuv420((const unsigned char *)frame, width, height,
				(unsigned char *)resized_img_buffer,
				d->req_width, d->req_height);
		*in = resized_img_buffer;
	} else
	{
		*in = frame;
	}
	return 0;
}

int pv_select_device(struct iaxc_video_driver *d, int input)
{
	return 0;
}

int pv_selected_device(struct iaxc_video_driver *d, int *input)
{
	return 0;
}

int pv_is_camera_working(struct iaxc_video_driver *d)
{
	return d->camera_working;
}

int pv_destroy(struct iaxc_video_driver *d )
{
	pv_stop(d);

	if ( waiting_frame )
	{
		free(waiting_frame);
		waiting_frame = 0;
	}

	if ( img_buffer )
	{
		free(img_buffer);
		img_buffer = 0;
	}

	if ( resized_img_buffer )
	{
		free(resized_img_buffer);
		resized_img_buffer = 0;
	}

	if ( converted_img_buffer )
	{
		free(converted_img_buffer);
		converted_img_buffer = 0;
	}
	return 0;
}

int iaxc_set_holding_frame(char *wf)
{
	// TODO: Some sort of check...
	// TODO: Mutex for thread safe operations??
	iaxci_usermsg(IAXC_TEXT_TYPE_NOTICE, "Using a new waiting frame");
	memcpy(waiting_frame, wf, width * height * 6 / 4);
	return 0;
}

/* initialize video driver */
int pv_initialize(struct iaxc_video_driver *d, int w, int h, int frate)
{
	/* setup methods */
	d->initialize = pv_initialize;
	d->destroy = pv_destroy;
	d->select_device = pv_select_device;
	d->selected_device = pv_selected_device;
	d->start = pv_start;
	d->stop = pv_stop;
	d->input = pv_input;
	d->is_camera_working = pv_is_camera_working;

	// HACK ALERT !!!
	// We initialize the camera with 320x240, since this is supported by
	// most cameras out there if the requested resolution is different,
	// we resize the frame in software.

	d->req_width = w;
	d->req_height = h;
	d->needs_resize = d->req_width != 320 || d->req_height != 240;

	width = 320;
	height = 240;
	framerate = frate;

	MUTEXINIT(&mytst);
	MUTEXINIT(&vdriverMutex);

	if ( d->needs_resize )
		resized_img_buffer = (char *)
			malloc(d->req_width * d->req_height * 3 / 2);

	//for yuy2 to yuv420 conversion
	converted_img_buffer = (char *) malloc(width * height * 2);

	// make buffer size large enough for YUY2 format
	img_buffer_size = width * height * 2;
	img_buffer = (char *) malloc(img_buffer_size);

	waiting_frame = (char *) malloc(width * height * 3 / 2);
	memset(waiting_frame, 0, width * height);
	memset(waiting_frame + width * height, 127,
			width * height / 2);

	return 0;
}

