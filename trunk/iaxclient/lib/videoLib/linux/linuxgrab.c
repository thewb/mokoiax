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
 * Peter Grayson <jpgrayson@gmail.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU Lesser (Library) General Public License
 */

/*
 * Module: video_portvideo
 * Purpose: Video code to provide portvideo driver support for IAX library
 * Developed by: Steve Kann
 * Creation Date: April 7, 2005
 */

#include "iaxclient_lib.h"
#include "libfg/capture.h"
#include "libfg/frame.h"
#include "../video_grab.h"

static int quit = 0;

static FRAMEGRABBER * fg = 0;
static FRAME * fg_frame = 0;

static MUTEX fg_lock;

static THREAD video_thread;
static THREAD polling_thread;

static grab_callback_t grab_callback;
static void * grab_callback_data;

static int width = 0;
static int height = 0;

static void grabber_release(void)
{
	if ( fg )
	{
		fg_close(fg);
		fg = 0;
	}

	if ( fg_frame )
	{
		frame_release(fg_frame);
		fg_frame = 0;
	}
}

static int pv_start_camera(int w, int h)
{
	fg = fg_open( NULL );

	if ( !fg )
		return -1;

	fg_set_format(fg, VIDEO_PALETTE_YUV420P);
	fg_set_capture_window(fg, 0, 0, w, h);

	if ( !fg_frame )
	{
		fg_frame = fg_new_compatible_frame(fg);

		if (!fg_frame)
		{
			fprintf(stderr, "%s: failed to allocate fg frame\n",
					__FUNCTION__);
			fg_close(fg);
			fg = 0;
			return -1;
		}
	}

	/* For diagnostic purposes... */
	/*fg_dump_info(fg);*/

	return 0;
}

static THREADFUNCDECL(pv_load_buffer)
{
	while ( !quit )
	{
		MUTEXLOCK(&fg_lock);

		if ( !fg )
		{
			MUTEXUNLOCK(&fg_lock);
			iaxc_millisleep(500);
			continue;
		}

		if ( !fg_grab_frame(fg, fg_frame) )
		{
			iaxci_usermsg(IAXC_TEXT_TYPE_NOTICE,
				"%s: camera not running or disconnected\n",
				__FUNCTION__);

			grabber_release();

			MUTEXUNLOCK(&fg_lock);
			continue;
		}

		MUTEXUNLOCK(&fg_lock);

		grab_callback(grab_callback_data, 0,
				(void *)fg_frame->data,
				frame_get_size(fg_frame));
	}

	return 0;
}

static THREADFUNCDECL(camera_polling)
{
	while ( !quit )
	{
		MUTEXLOCK(&fg_lock);

		if ( !fg )
		{
			pv_start_camera(width, height);
		}

		MUTEXUNLOCK(&fg_lock);

		iaxc_millisleep(1000);
	}

	return 0;
}

void *grabber_init( grab_callback_t gc, void *gc_data, int w, int h)
{
	int got_camera;

	grab_callback = gc;
	grab_callback_data = gc_data;
	width = w;
	height = h;

	quit = 0;

	/* Make a single attempt to start the camera before kicking-off
	 * the polling thread. We want to get video frames coming in as
	 * soon as possible.
	 */
	if ( pv_start_camera(width, height) < 0 )
	{
		iaxci_usermsg(IAXC_TEXT_TYPE_NOTICE,
				"failed to start camera, will keep trying\n");
		got_camera = 0;
	} else
		got_camera = 1;

	MUTEXINIT(&fg_lock);

	if (THREADCREATE(pv_load_buffer, NULL, video_thread, ThID) == THREADCREATE_ERROR)
	{
		fprintf(stderr,"Could not start grabber thread!\n");
		return NULL;
	}

	if (THREADCREATE(camera_polling, NULL, polling_thread, ThID) == THREADCREATE_ERROR)
	{
		fprintf(stderr,"Could not start polling thread!\n");
		return NULL;
	}

	/* In theory, we would pass back an opaque context here, but we keep
	 * everything static global, so we just need to return something
	 * non-zero to indicate success.
	 */
	if ( got_camera )
		return (void *)-1;
	else
		return NULL;
}

void grabber_destroy(void *d)
{
	quit = 1;

	THREADJOIN(video_thread);
	THREADJOIN(polling_thread);

	MUTEXLOCK(&fg_lock);
	grabber_release();
	MUTEXUNLOCK(&fg_lock);

	MUTEXDESTROY(&fg_lock);
}

// dummy functions
int grabber_format_check(unsigned int format)
{
	if ( format == IMAGE_FORMAT_I420 )
		return 1;

	if ( format == IMAGE_FORMAT_IYUV )
		return 1;

	return 0;
}

int listVidCapDevices(char *buff, int buffSize)
{
	if (buffSize > 0 )
		buff[0] = 0;

	return 0;
}

