/*
 * iaxclient: a cross-platform IAX softphone library
 *
 * Copyrights:
 * Copyright (C) 2003-2006, Horizon Wimba, Inc.
 * Copyright (C) 2007, Wimba, Inc.
 *
 * Contributors:
 * Steve Kann <stevek@stevek.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU Lesser (Library) General Public License.
 */

#ifndef _AUDIO_ENCODE_H
#define _AUDIO_ENCODE_H

struct iaxc_call;
struct iax_event;

int audio_send_encoded_audio(struct iaxc_call * most_recent_answer, int callNo,
		void * data, int iEncodeType, int samples);

int audio_decode_audio(struct iaxc_call * p, void * out, void * data, int len,
		int iEncodeType, int * samples);

#endif

