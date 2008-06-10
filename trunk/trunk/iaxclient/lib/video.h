/*
 * iaxclient: a cross-platform IAX softphone library
 *
 * Copyrights:
 * Copyright (C) 2007, Wimba, Inc.
 *
 * Contributors:
 * Peter Grayson <jpgrayson@gmail.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU Lesser (Library) General Public License.
 */

#ifndef __VIDEO_H__
#define __VIDEO_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

struct iaxc_call;
struct iaxc_video_codec;
struct iaxc_video_stats;

struct iaxc_video_driver {
	/* data */
	char *name; 	/* driver name */
	void *priv;	/* pointer to private data */

	/* methods */
	int (*initialize)(struct iaxc_video_driver *d, int w, int h, int framerate);
	int (*destroy)(struct iaxc_video_driver *d);  /* free resources */
	int (*select_device)(struct iaxc_video_driver *d, int input);
	int (*selected_device)(struct iaxc_video_driver *d, int *input);
	int (*start)(struct iaxc_video_driver *d);
	int (*stop)(struct iaxc_video_driver *d);
	int (*input)(struct iaxc_video_driver *d, char **data);
	int (*is_camera_working)(struct iaxc_video_driver *d);

	/* HACK ALERT!!!
	 * If the requested size of the video is not 320x240, then we
	 * resize the image in software rather than relying on the camera
	 * to provide an adequate frame.
	 */
	int needs_resize;
	int req_width;
	int req_height;
	int camera_working; /* true if the camera is working */
};

int video_initialize(void);

int video_destroy(void);

int video_get_stats(struct iaxc_call * call,
		struct iaxc_video_stats * stats, int reset);

void video_reset_codec_stats(struct iaxc_video_codec * codec);

int video_send_video(struct iaxc_call *, int);

int video_recv_video(struct iaxc_call * call, int sel_call,
		void * encoded_video, int encoded_video_len,
		unsigned int ts, int format);

#endif
