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

#ifndef VIDEO_GRAB_H
#define VIDEO_GRAB_H

// standard (and equivalent) image formats
#define IMAGE_FORMAT_I420	0x30323449
#define IMAGE_FORMAT_IYUV	0x56555949

// alternate image format also supported
#define IMAGE_FORMAT_YUY2	0x32595559

#ifdef __cplusplus
extern "C" {
#endif

struct iaxc_video_driver;

typedef void (*grab_callback_t)(void * ctx, double sample_time,
		void * buf, long buf_size);

void *grabber_init(grab_callback_t gc, void *gc_data, int w, int h);
void grabber_destroy(void * opaque);
int grabber_format_check(unsigned int format);
int listVidCapDevices(char *charBuff, int buffSize);


int pv_initialize(struct iaxc_video_driver *d, int w, int h, int framerate);

#ifdef __cplusplus
}
#endif

#endif

