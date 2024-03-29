/* $Id: mcalign-test.c 1971 2007-10-28 19:13:50Z lennart $ */

/***
  This file is part of PulseAudio.

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <pulsecore/core-util.h>
#include <pulsecore/mcalign.h>
#include <pulsecore/gccmacro.h>

/* A simple program for testing pa_mcalign */

int main(PA_GCC_UNUSED int argc, PA_GCC_UNUSED char *argv[]) {
    pa_mempool *p;
    pa_mcalign *a;
    pa_memchunk c;

    p = pa_mempool_new(0);

    a = pa_mcalign_new(11);

    pa_memchunk_reset(&c);

    srand(time(NULL));

    for (;;) {
        ssize_t r;
        size_t l;

        if (!c.memblock) {
            c.memblock = pa_memblock_new(p, 2048);
            c.index = c.length = 0;
        }

        assert(c.index < pa_memblock_get_length(c.memblock));

        l = pa_memblock_get_length(c.memblock) - c.index;

        l = l <= 1 ? l : rand() % (l-1) +1 ;

        p = pa_memblock_acquire(c.memblock);

        if ((r = read(STDIN_FILENO, (uint8_t*) p + c.index, l)) <= 0) {
            pa_memblock_release(c.memblock);
            fprintf(stderr, "read() failed: %s\n", r < 0 ? strerror(errno) : "EOF");
            break;
        }

        pa_memblock_release(c.memblock);

        c.length = r;
        pa_mcalign_push(a, &c);
        fprintf(stderr, "Read %ld bytes\n", (long)r);

        c.index += r;

        if (c.index >= pa_memblock_get_length(c.memblock)) {
            pa_memblock_unref(c.memblock);
            pa_memchunk_reset(&c);
        }

        for (;;) {
            pa_memchunk t;

            if (pa_mcalign_pop(a, &t) < 0)
                break;

            p = pa_memblock_acquire(t.memblock);
            pa_loop_write(STDOUT_FILENO, (uint8_t*) p + t.index, t.length, NULL);
            pa_memblock_release(t.memblock);
            fprintf(stderr, "Wrote %lu bytes.\n", (unsigned long) t.length);

            pa_memblock_unref(t.memblock);
        }
    }

    pa_mcalign_free(a);

    if (c.memblock)
        pa_memblock_unref(c.memblock);

    pa_mempool_free(p);

    return 0;
}
