/*
 * iaxclient: a cross-platform IAX softphone library
 *
 * Copyrights:
 * Copyright (C) 2008 - Brandon Kruse
 * Copyright (C) 2008 - Digium, Inc. 
 *
 * Contributors:
 * Brandon Kruse <bkruse@openmoko.org>
 *
 * This program is free software, distributed under the terms of
 * the GNU Lesser (Library) General Public License.
 */

#ifndef _AUDIO_PULSEAUDIO_H
#define _AUDIO_PULSEAUDIO_H

#include "iaxclient_lib.h"

/* pulse audio init */
int pulse_initialize(struct iaxc_audio_driver *d, int sample_rate);

#endif
