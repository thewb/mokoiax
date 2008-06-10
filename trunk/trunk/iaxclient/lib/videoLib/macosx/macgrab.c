/*
 * iaxclient: a cross-platform IAX softphone library
 *
 * Copyrights:
 * Copyright (c) 2004, Daniel Heckenberg. All rights reserved.
 * Copyright (c) 2005, Tipic, Inc. All rights reserved.
 * Copyright (C) 2006, Horizon Wimba, Inc.
 * Copyright (C) 2007, Wimba, Inc.
 *
 * Contributors:
 * Daniel Heckenberg <danielh.seeSaw<at>cse<dot>unsw<dot>edu<dot>au>
 * Francesco Delfino <pluto@tipic.com>
 * Mihai Balea <mihai AT hates DOT ms>
 * Peter Grayson <jpgrayson@gmail.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU Lesser (Library) General Public License.
 */

/*
 *  code based on videoWindow.c from seeSaw
 *
 *  Created by Daniel Heckenberg.
 *  Copyright (c) 2004 Daniel Heckenberg. All rights reserved.
 *  (danielh.seeSaw<at>cse<dot>unsw<dot>edu<dot>au)
 *
 *  Adapted for iaxclient by Francesco Delfino.
 *  Copyright (c) 2005 Tipic, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the right to use, copy, modify, merge, publish, communicate, sublicence,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * TO THE EXTENT PERMITTED BY APPLICABLE LAW, THIS SOFTWARE IS PROVIDED
 * "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "iaxclient_lib.h"
#include "../video_grab.h"
#include "vdigGrab.h"

typedef struct macgrabber
{
	long		milliSecPerTimer;
	int w, h;
	THREADID procThreadID;
	THREAD procThread;
	grab_callback_t gc;
	void *gc_data;

	// vdig grab data
	VdigGrab* pVdg;
	long		milliSecPerFrame;
	Fixed		frameRate;
	long		bytesPerSecond;
	ImageDescriptionHandle vdImageDesc;

	// Destination port (GWorld)
	CGrafPtr	dstPort;
	Rect		dstPortBounds;
	Rect		dstDisplayBounds;
	void*		pDstData;
	long		dstDataSize;

	void*		pYUV420;
	long		yuv420DataSize;

	volatile bool stop_thread;
	volatile bool thread_running;
} macgrabber_t;

const int gMilliSecPerTimerMin = 20;
const int gMilliSecPerTimerMax = 100;

OSErr
videoSetTimerPeriodFromVdig(macgrabber_t* pWnd)
{
	OSErr err;

	// ASC soft vdig fails this call
	err = vdgGetDataRate(pWnd->pVdg,
			&pWnd->milliSecPerFrame,
			&pWnd->frameRate,
			&pWnd->bytesPerSecond);
	if (err)
	{
		iaxci_usermsg(IAXC_TEXT_TYPE_ERROR,
				"vdgGetDataRate err=%d\n", err);
	}

	if (err == noErr)
	{
		// Some vdigs do return the frameRate but not the milliSecPerFrame
		if ((pWnd->milliSecPerFrame == 0) && (pWnd->frameRate != 0))
		{
			pWnd->milliSecPerFrame = (1000L << 16) / pWnd->frameRate;
		}
	}

	// Poll the vdig at twice the frame rate or between sensible limits
	pWnd->milliSecPerTimer = pWnd->milliSecPerFrame / 2;
	if (pWnd->milliSecPerTimer <= 0)
	{
		iaxci_usermsg(IAXC_TEXT_TYPE_ERROR,
				"pWnd->milliSecPerFrame: %d ",
				pWnd->milliSecPerFrame);
		pWnd->milliSecPerTimer = gMilliSecPerTimerMin;
		iaxci_usermsg(IAXC_TEXT_TYPE_ERROR,
				"forcing timer period to %dms\n",
				pWnd->milliSecPerTimer);
	}
	if (pWnd->milliSecPerTimer >= gMilliSecPerTimerMax)
	{
		iaxci_usermsg(IAXC_TEXT_TYPE_ERROR,
				"pWnd->milliSecPerFrame: %d ",
				pWnd->milliSecPerFrame);
		pWnd->milliSecPerFrame = gMilliSecPerTimerMax;
		iaxci_usermsg(IAXC_TEXT_TYPE_ERROR,
				"forcing timer period to %dms\n",
				pWnd->milliSecPerTimer);
	}

	return err;
}

OSErr
videoInitOffscreen (macgrabber_t* pWnd, int width, int height)
{
	OSErr err;

	// create the offscreen target
	pWnd->dstPortBounds.left = 0;
	pWnd->dstPortBounds.right = width;
	pWnd->dstPortBounds.top = 0;
	pWnd->dstPortBounds.bottom = height;

	err = createOffscreenGWorld(&pWnd->dstPort,
			k422YpCbCr8CodecType,
			&pWnd->dstPortBounds);
	if (err)
	{
		iaxci_usermsg(IAXC_TEXT_TYPE_ERROR,
				"createOffscreenGWorld err=%d\n",
				err);
		return err;
	}

	// Get buffer from GWorld
	pWnd->pDstData = GetPixBaseAddr(GetGWorldPixMap(pWnd->dstPort));
	pWnd->dstDataSize = GetPixRowBytes(GetGWorldPixMap(pWnd->dstPort)) *
		(pWnd->dstPortBounds.bottom - pWnd->dstPortBounds.top);
	pWnd->dstDisplayBounds = pWnd->dstPortBounds;

	return 0;
}

void
videoSetDisplayBoundsToCaptureBounds(macgrabber_t* pWnd)
{
	// TODO offset from port bounds?
	pWnd->dstDisplayBounds.left = 0;
	pWnd->dstDisplayBounds.right = pWnd->w;
	pWnd->dstDisplayBounds.top = 0;
	pWnd->dstDisplayBounds.bottom = pWnd->h;
}


static void uyvy2yuv420( unsigned char *pBuffer,  unsigned char *img_buffer, int w, int h)
{
	int i, j ;
	// is the size of the input "uyvy" buffer: 16bit x pixel
	int lBufferSize = w * h * 2;
	int RowLen = w * 2;
	unsigned char *y = img_buffer;
	unsigned char *u = img_buffer + lBufferSize/2;
	unsigned char *v = u + lBufferSize/8;
	unsigned char *src = pBuffer;

	for (j=0; j< h; j+=1)
	{
		src = pBuffer + j * RowLen;
		for (i=0; i < RowLen; i+=4) {
			*(y++)=src[i+1];
			*(y++)=src[i+3];

			if (j%2== 0) {
				*(u++) = (unsigned char) ((0L + src[i] + src[i+RowLen])>>1);
				*(v++) = (unsigned char) ((0L + src[i+2] + src[i+2+RowLen])>>1);
			}
		}
	}
}


THREADFUNCDECL(grabthread)
{
	THREADFUNCRET(ret);
	macgrabber_t *pWnd = (macgrabber_t *)args;

	while ( !pWnd->stop_thread )
	{
		int isUpdated = 0;

		// check if we're grabbing
		if (vdgIsGrabbing(pWnd->pVdg))
		{
			if (vdgIdle( pWnd->pVdg, &isUpdated))
			{
				//TODO should send waiting frame ?
				continue;
			}

			if (isUpdated)
			{
				// TODO synchronization of texture update
				uyvy2yuv420((unsigned char *)pWnd->pDstData,
						(unsigned char *)pWnd->pYUV420,
						pWnd->w, pWnd->h);
				pWnd->gc(pWnd->gc_data, 0,
						(void *)pWnd->pYUV420,
						pWnd->yuv420DataSize);
			}
		}

		iaxc_millisleep( pWnd->milliSecPerTimer );
	}
	return ret;
}


void *grabber_init(grab_callback_t gc, void *gc_data, int w, int h)
{
	macgrabber_t *pWnd;
	pWnd = (macgrabber_t *) calloc(1, sizeof(macgrabber_t));
	if ( !pWnd)
		return 0;

	pWnd->w = w;
	pWnd->h = h;
	pWnd->gc = gc;
	pWnd->gc_data= gc_data;

	pWnd->stop_thread = false;
	pWnd->thread_running = false;

	pWnd->yuv420DataSize = w * h * 6 / 4;
	pWnd->pYUV420 = calloc(1, pWnd->yuv420DataSize);
	if ( !pWnd->pYUV420 )
	{
		iaxci_usermsg(IAXC_TEXT_TYPE_ERROR,
				"cannot allocate YUV420 buffer\n");
		goto bail;
	}

	// Create the vdig grabber
	OSErr err;

	if ( !(pWnd->pVdg = vdgNew()) )
	{
		iaxci_usermsg(IAXC_TEXT_TYPE_ERROR,
				"vdgNew: failed to allocate\n");
		goto bail;
	}

	if ( (err = vdgInit(pWnd->pVdg)) )
	{
		if ( err == couldntGetRequiredComponent )
			iaxci_usermsg(IAXC_TEXT_TYPE_ERROR, "no camera available");
		else
			iaxci_usermsg(IAXC_TEXT_TYPE_ERROR, "vdgInit err=%d\n", err);
		goto bail;
	}


	if ( (err = vdgRequestSettings(pWnd->pVdg)) )

	{
		iaxci_usermsg(IAXC_TEXT_TYPE_ERROR,
				"vdgRequestSettings err=%d\n", err);
		goto bail;
	}

	if ( (err = vdgPreflightGrabbing(pWnd->pVdg, w, h)) )
	{
		iaxci_usermsg(IAXC_TEXT_TYPE_ERROR,
				"vdgPreflightGrabbing err=%d\n", err);
		goto bail;
	}

	// Set the timer frequency from the Vdig's Data Rate
	videoSetTimerPeriodFromVdig(pWnd);

	pWnd->vdImageDesc = (ImageDescriptionHandle)NewHandle(0);
	if ( (err = vdgGetImageDescription(pWnd->pVdg, pWnd->vdImageDesc)) )
	{
		iaxci_usermsg(IAXC_TEXT_TYPE_ERROR,
				"vdgGetImageDescription err=%d\n", err);
		goto bail;
	}

	// Now create the offscreen GWorld to match the texture
	if ( (err = videoInitOffscreen(pWnd, pWnd->w, pWnd->h)) )
	{
		iaxci_usermsg(IAXC_TEXT_TYPE_ERROR,
				"videoInitOffscreen err=%d\n", err);
		goto bail;
	}

	// Set the decompression destination to the offscreen GWorld
	if ( (err = vdgSetDestination(pWnd->pVdg, pWnd->dstPort)) )
	{
		iaxci_usermsg(IAXC_TEXT_TYPE_ERROR,
				"vdgGetImageDescription err=%d\n", err);
		goto bail;
	}

	// Set the display bounds to match only the part of the texture
	// that corresponds to the video image
	videoSetDisplayBoundsToCaptureBounds(pWnd);

	if ( (err = vdgStartGrabbing(pWnd->pVdg)) )
	{
		iaxci_usermsg(IAXC_TEXT_TYPE_ERROR,
				"vdgStartGrabbing err=%d\n", err);
		goto bail;
	}

	// Do the first poll straight away
	//glutTimerFunc(0, videoTimerDispatch, pWnd );
	if ( THREADCREATE(grabthread, pWnd, pWnd->procThread,
				pWnd->procThreadID) == THREADCREATE_ERROR)
	{
		iaxci_usermsg(IAXC_TEXT_TYPE_ERROR,
				"Cannot create grabthread\n", err);
		goto bail;
	}

	pWnd->thread_running = true;

	iaxci_usermsg(IAXC_TEXT_TYPE_NOTICE,
			"Grabber initialized and started\n", err);
	return pWnd;

bail:
	grabber_destroy(pWnd);
	return 0;
}

void grabber_destroy( void *opaque )
{
	if ( !opaque )
		return;

	macgrabber_t *pWnd = (macgrabber_t *) opaque;
	pWnd->stop_thread = true;

	if ( pWnd->thread_running )
		THREADJOIN(pWnd->procThread);

	if ( pWnd->dstPort )
	{
		disposeOffscreenGWorld(pWnd->dstPort);
		pWnd->dstPort = NULL;
	}

	if ( pWnd->vdImageDesc )
	{
		DisposeHandle((Handle)pWnd->vdImageDesc);
		pWnd->vdImageDesc = NULL;
	}

	if ( pWnd->pVdg )
	{
		vdgStopGrabbing(pWnd->pVdg);
		vdgUninit(pWnd->pVdg);
		vdgDelete(pWnd->pVdg);
		pWnd->pVdg = NULL;
	}

	if ( pWnd->pYUV420 )
	{
		free( pWnd->pYUV420 );
		pWnd->pYUV420 = NULL;
	}

	free(pWnd);
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

