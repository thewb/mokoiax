/* $Id: operation.c 2067 2007-11-21 01:30:40Z lennart $ */

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/xmalloc.h>
#include <pulsecore/macro.h>
#include <pulsecore/flist.h>

#include "internal.h"
#include "operation.h"

PA_STATIC_FLIST_DECLARE(operations, 0, pa_xfree);

pa_operation *pa_operation_new(pa_context *c, pa_stream *s, pa_operation_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_assert(c);

    if (!(o = pa_flist_pop(PA_STATIC_FLIST_GET(operations))))
        o = pa_xnew(pa_operation, 1);

    PA_REFCNT_INIT(o);
    o->context = c;
    o->stream = s;
    o->private = NULL;

    o->state = PA_OPERATION_RUNNING;
    o->callback = cb;
    o->userdata = userdata;

    /* Refcounting is strictly one-way: from the "bigger" to the "smaller" object. */
    PA_LLIST_PREPEND(pa_operation, c->operations, o);
    pa_operation_ref(o);

    return o;
}

pa_operation *pa_operation_ref(pa_operation *o) {
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    PA_REFCNT_INC(o);
    return o;
}
void pa_operation_unref(pa_operation *o) {
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    if (PA_REFCNT_DEC(o) <= 0) {
        pa_assert(!o->context);
        pa_assert(!o->stream);

        if (pa_flist_push(PA_STATIC_FLIST_GET(operations), o) < 0)
            pa_xfree(o);
    }
}

static void operation_set_state(pa_operation *o, pa_operation_state_t st) {
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    if (st == o->state)
        return;

    pa_operation_ref(o);

    o->state = st;

    if ((o->state == PA_OPERATION_DONE) || (o->state == PA_OPERATION_CANCELED)) {

        if (o->context) {
            pa_assert(PA_REFCNT_VALUE(o) >= 2);

            PA_LLIST_REMOVE(pa_operation, o->context->operations, o);
            pa_operation_unref(o);
        }

        o->context = NULL;
        o->stream = NULL;
        o->callback = NULL;
        o->userdata = NULL;
    }

    pa_operation_unref(o);
}

void pa_operation_cancel(pa_operation *o) {
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    operation_set_state(o, PA_OPERATION_CANCELED);
}

void pa_operation_done(pa_operation *o) {
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    operation_set_state(o, PA_OPERATION_DONE);
}

pa_operation_state_t pa_operation_get_state(pa_operation *o) {
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    return o->state;
}
