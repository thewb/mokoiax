/* $Id: msgobject.c 1971 2007-10-28 19:13:50Z lennart $ */

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

#include "msgobject.h"

PA_DEFINE_CHECK_TYPE(pa_msgobject, pa_object);

pa_msgobject *pa_msgobject_new_internal(size_t size, const char *type_name, int (*check_type)(const char *type_name)) {
    pa_msgobject *o;

    pa_assert(size > sizeof(pa_msgobject));
    pa_assert(type_name);

    if (!check_type)
        check_type = pa_msgobject_check_type;

    pa_assert(check_type(type_name));
    pa_assert(check_type("pa_object"));
    pa_assert(check_type("pa_msgobject"));

    o = PA_MSGOBJECT(pa_object_new_internal(size, type_name, check_type));
    o->process_msg = NULL;
    return o;
}
