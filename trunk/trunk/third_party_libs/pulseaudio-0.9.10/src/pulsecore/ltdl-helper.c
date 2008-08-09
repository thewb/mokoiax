/* $Id: ltdl-helper.c 1971 2007-10-28 19:13:50Z lennart $ */

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <ctype.h>

#include <pulse/xmalloc.h>
#include <pulse/util.h>

#include <pulsecore/core-util.h>
#include <pulsecore/macro.h>

#include "ltdl-helper.h"

pa_void_func_t pa_load_sym(lt_dlhandle handle, const char *module, const char *symbol) {
    char *sn, *c;
    pa_void_func_t f;

    pa_assert(handle);
    pa_assert(module);
    pa_assert(symbol);

    if ((f = ((pa_void_func_t) (long) lt_dlsym(handle, symbol))))
        return f;

    /* As the .la files might have been cleansed from the system, we should
     * try with the ltdl prefix as well. */

    sn = pa_sprintf_malloc("%s_LTX_%s", module, symbol);

    for (c = sn; *c; c++)
        if (!isalnum(*c))
            *c = '_';

    f = (pa_void_func_t) (long) lt_dlsym(handle, sn);
    pa_xfree(sn);

    return f;
}