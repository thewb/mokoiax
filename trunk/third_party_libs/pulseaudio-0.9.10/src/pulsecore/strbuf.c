/* $Id: strbuf.c 1971 2007-10-28 19:13:50Z lennart $ */

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

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include <pulse/xmalloc.h>
#include <pulsecore/macro.h>

#include "strbuf.h"

/* A chunk of the linked list that makes up the string */
struct chunk {
    struct chunk *next;
    size_t length;
};

#define CHUNK_TO_TEXT(c) ((char*) (c) + PA_ALIGN(sizeof(struct chunk)))

struct pa_strbuf {
    size_t length;
    struct chunk *head, *tail;
};

pa_strbuf *pa_strbuf_new(void) {
    pa_strbuf *sb;

    sb = pa_xnew(pa_strbuf, 1);
    sb->length = 0;
    sb->head = sb->tail = NULL;

    return sb;
}

void pa_strbuf_free(pa_strbuf *sb) {
    pa_assert(sb);

    while (sb->head) {
        struct chunk *c = sb->head;
        sb->head = sb->head->next;
        pa_xfree(c);
    }

    pa_xfree(sb);
}

/* Make a C string from the string buffer. The caller has to free
 * string with pa_xfree(). */
char *pa_strbuf_tostring(pa_strbuf *sb) {
    char *t, *e;
    struct chunk *c;

    pa_assert(sb);

    e = t = pa_xnew(char, sb->length+1);

    for (c = sb->head; c; c = c->next) {
        pa_assert((size_t) (e-t) <= sb->length);
        memcpy(e, CHUNK_TO_TEXT(c), c->length);
        e += c->length;
    }

    /* Trailing NUL */
    *e = 0;

    pa_assert(e == t+sb->length);

    return t;
}

/* Combination of pa_strbuf_free() and pa_strbuf_tostring() */
char *pa_strbuf_tostring_free(pa_strbuf *sb) {
    char *t;

    pa_assert(sb);
    t = pa_strbuf_tostring(sb);
    pa_strbuf_free(sb);

    return t;
}

/* Append a string to the string buffer */
void pa_strbuf_puts(pa_strbuf *sb, const char *t) {

    pa_assert(sb);
    pa_assert(t);

    pa_strbuf_putsn(sb, t, strlen(t));
}

/* Append a new chunk to the linked list */
static void append(pa_strbuf *sb, struct chunk *c) {
    pa_assert(sb);
    pa_assert(c);

    if (sb->tail) {
        pa_assert(sb->head);
        sb->tail->next = c;
    } else {
        pa_assert(!sb->head);
        sb->head = c;
    }

    sb->tail = c;
    sb->length += c->length;
    c->next = NULL;
}

/* Append up to l bytes of a string to the string buffer */
void pa_strbuf_putsn(pa_strbuf *sb, const char *t, size_t l) {
    struct chunk *c;

    pa_assert(sb);
    pa_assert(t);

    if (!l)
       return;

    c = pa_xmalloc(PA_ALIGN(sizeof(struct chunk)) + l);
    c->length = l;
    memcpy(CHUNK_TO_TEXT(c), t, l);

    append(sb, c);
}

/* Append a printf() style formatted string to the string buffer. */
/* The following is based on an example from the GNU libc documentation */
int pa_strbuf_printf(pa_strbuf *sb, const char *format, ...) {
    int size = 100;
    struct chunk *c = NULL;

    pa_assert(sb);
    pa_assert(format);

    for(;;) {
        va_list ap;
        int r;

        c = pa_xrealloc(c, PA_ALIGN(sizeof(struct chunk)) + size);

        va_start(ap, format);
        r = vsnprintf(CHUNK_TO_TEXT(c), size, format, ap);
        CHUNK_TO_TEXT(c)[size-1] = 0;
        va_end(ap);

        if (r > -1 && r < size) {
            c->length = r;
            append(sb, c);
            return r;
        }

        if (r > -1)    /* glibc 2.1 */
            size = r+1;
        else           /* glibc 2.0 */
            size *= 2;
    }
}
