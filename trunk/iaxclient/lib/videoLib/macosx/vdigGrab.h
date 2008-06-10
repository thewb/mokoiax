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
 *  vdigGrab.h
 *  seeSaw
 *
 *  Created by Daniel Heckenberg.
 *  Copyright (c) 2004 Daniel Heckenberg. All rights reserved.
 *  (danielh.seeSaw<at>cse<dot>unsw<dot>edu<dot>au)
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

#ifndef VDIGGRAB_H
#define VDIGGRAB_H

#include <Carbon/Carbon.h>
#include <QuickTime/QuickTime.h>

typedef struct tagVdigGrab VdigGrab;

VdigGrab*
vdgNew();

OSErr
vdgInit(VdigGrab* pVdg);

OSErr
vdgRequestSettings(VdigGrab* pVdg);

OSErr
vdgGetDeviceNameAndFlags(VdigGrab* pVdg, char* szName, long* pBuffSize, UInt32* pVdFlags);

OSErr
vdgPreflightGrabbing(VdigGrab* pVdg, int w, int h);

OSErr
vdgStartGrabbing(VdigGrab* pVdg);

OSErr
vdgGetDataRate( VdigGrab* pVdg,
		long* pMilliSecPerFrame,
		Fixed* pFramesPerSecond,
		long* pBytesPerSecond);

OSErr
vdgGetImageDescription( VdigGrab* pVdg,
		ImageDescriptionHandle vdImageDesc );

OSErr
vdgSetDestination( VdigGrab* pVdg, CGrafPtr dstPort );

OSErr
vdgDecompressionSequenceBegin( VdigGrab* pVdg,
		CGrafPtr dstPort,
		Rect* pDstRect,
		MatrixRecord* pDstScaleMatrix );

OSErr
vdgDecompressionSequenceWhen( VdigGrab* pVdg,
		Ptr theData,
		long dataSize);

OSErr
vdgDecompressionSequenceEnd( VdigGrab* pVdg );

OSErr
vdgPoll( VdigGrab* pVdg,
		UInt8*		pQueuedFrameCount,
		Ptr*		pTheData,
		long*		pDataSize,
		UInt8*		pSimilarity,
		TimeRecord*	pTime );

OSErr
vdgIdle(VdigGrab* pVdg, int* pIsUpdated);

OSErr
vdgStopGrabbing(VdigGrab* pVdg);

bool
vdgIsGrabbing(VdigGrab* pVdg);

OSErr
vdgReleaseBuffer(VdigGrab* pVdg, Ptr theData);

OSErr
vdgUninit(VdigGrab* pVdg);

void
vdgDelete(VdigGrab* pVdg);

OSErr
createOffscreenGWorld( GWorldPtr* pGWorldPtr,
		OSType		pixelFormat,
		Rect*		pBounds);

void
disposeOffscreenGWorld( GWorldPtr gworld);

#endif //VDIGGRAB_H
