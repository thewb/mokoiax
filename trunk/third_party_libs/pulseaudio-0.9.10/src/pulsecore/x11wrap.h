#ifndef foox11wraphfoo
#define foox11wraphfoo

/* $Id: x11wrap.h 1426 2007-02-13 15:35:19Z ossman $ */

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#include <X11/Xlib.h>

#include <pulsecore/core.h>

typedef struct pa_x11_wrapper pa_x11_wrapper;

/* Return the X11 wrapper for this core. In case no wrapper was
    existant before, allocate a new one */
pa_x11_wrapper* pa_x11_wrapper_get(pa_core *c, const char *name);

/* Increase the wrapper's reference count by one */
pa_x11_wrapper* pa_x11_wrapper_ref(pa_x11_wrapper *w);

/* Decrease the reference counter of an X11 wrapper object */
void pa_x11_wrapper_unref(pa_x11_wrapper* w);

/* Return the X11 display object for this connection */
Display *pa_x11_wrapper_get_display(pa_x11_wrapper *w);

typedef struct pa_x11_client pa_x11_client;

/* Register an X11 client, that is called for each X11 event */
pa_x11_client* pa_x11_client_new(pa_x11_wrapper *w, int (*cb)(pa_x11_wrapper *w, XEvent *e, void *userdata), void *userdata);

/* Free an X11 client object */
void pa_x11_client_free(pa_x11_client *c);

#endif
