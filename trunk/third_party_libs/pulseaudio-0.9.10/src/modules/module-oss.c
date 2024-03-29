/* $Id: module-oss.c 2066 2007-11-21 01:21:53Z lennart $ */

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

/* General power management rules:
 *
 *   When SUSPENDED we close the audio device.
 *
 *   We make no difference between IDLE and RUNNING in our handling.
 *
 *   As long as we are in RUNNING/IDLE state we will *always* write data to
 *   the device. If none is avilable from the inputs, we write silence
 *   instead.
 *
 *   If power should be saved on IDLE module-suspend-on-idle should be used.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#include <sys/soundcard.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <poll.h>

#include <pulse/xmalloc.h>
#include <pulse/util.h>

#include <pulsecore/core-error.h>
#include <pulsecore/thread.h>
#include <pulsecore/sink.h>
#include <pulsecore/source.h>
#include <pulsecore/module.h>
#include <pulsecore/sample-util.h>
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>
#include <pulsecore/thread-mq.h>
#include <pulsecore/rtpoll.h>

#include "oss-util.h"
#include "module-oss-symdef.h"

PA_MODULE_AUTHOR("Lennart Poettering");
PA_MODULE_DESCRIPTION("OSS Sink/Source");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(FALSE);
PA_MODULE_USAGE(
        "sink_name=<name for the sink> "
        "source_name=<name for the source> "
        "device=<OSS device> "
        "record=<enable source?> "
        "playback=<enable sink?> "
        "format=<sample format> "
        "channels=<number of channels> "
        "rate=<sample rate> "
        "fragments=<number of fragments> "
        "fragment_size=<fragment size> "
        "channel_map=<channel map> "
        "mmap=<enable memory mapping?>");

#define DEFAULT_DEVICE "/dev/dsp"

struct userdata {
    pa_core *core;
    pa_module *module;
    pa_sink *sink;
    pa_source *source;

    pa_thread *thread;
    pa_thread_mq thread_mq;
    pa_rtpoll *rtpoll;

    char *device_name;

    pa_memchunk memchunk;

    size_t frame_size;
    uint32_t in_fragment_size, out_fragment_size, in_nfrags, out_nfrags, in_hwbuf_size, out_hwbuf_size;
    pa_bool_t use_getospace, use_getispace;
    pa_bool_t use_getodelay;

    pa_bool_t sink_suspended, source_suspended;

    int fd;
    int mode;

    int mixer_fd;
    int mixer_devmask;

    int nfrags, frag_size;

    pa_bool_t use_mmap;
    unsigned out_mmap_current, in_mmap_current;
    void *in_mmap, *out_mmap;
    pa_memblock **in_mmap_memblocks, **out_mmap_memblocks;

    int in_mmap_saved_nfrags, out_mmap_saved_nfrags;

    pa_rtpoll_item *rtpoll_item;
};

static const char* const valid_modargs[] = {
    "sink_name",
    "source_name",
    "device",
    "record",
    "playback",
    "fragments",
    "fragment_size",
    "format",
    "rate",
    "channels",
    "channel_map",
    "mmap",
    NULL
};

static void trigger(struct userdata *u, pa_bool_t quick) {
    int enable_bits = 0, zero = 0;

    pa_assert(u);

    if (u->fd < 0)
        return;

     pa_log_debug("trigger");

    if (u->source && PA_SOURCE_OPENED(u->source->thread_info.state))
        enable_bits |= PCM_ENABLE_INPUT;

    if (u->sink && PA_SINK_OPENED(u->sink->thread_info.state))
        enable_bits |= PCM_ENABLE_OUTPUT;

    pa_log_debug("trigger: %i", enable_bits);


    if (u->use_mmap) {

        if (!quick)
            ioctl(u->fd, SNDCTL_DSP_SETTRIGGER, &zero);

#ifdef SNDCTL_DSP_HALT
        if (enable_bits == 0)
            if (ioctl(u->fd, SNDCTL_DSP_HALT, NULL) < 0)
                pa_log_warn("SNDCTL_DSP_HALT: %s", pa_cstrerror(errno));
#endif

        if (ioctl(u->fd, SNDCTL_DSP_SETTRIGGER, &enable_bits) < 0)
            pa_log_warn("SNDCTL_DSP_SETTRIGGER: %s", pa_cstrerror(errno));

        if (u->sink && !(enable_bits & PCM_ENABLE_OUTPUT)) {
            pa_log_debug("clearing playback buffer");
            pa_silence_memory(u->out_mmap, u->out_hwbuf_size, &u->sink->sample_spec);
        }

    } else {

        if (enable_bits)
            if (ioctl(u->fd, SNDCTL_DSP_POST, NULL) < 0)
                pa_log_warn("SNDCTL_DSP_POST: %s", pa_cstrerror(errno));

        if (!quick) {
            /*
             * Some crappy drivers do not start the recording until we
             * read something.  Without this snippet, poll will never
             * register the fd as ready.
             */

            if (u->source && PA_SOURCE_OPENED(u->source->thread_info.state)) {
                uint8_t *buf = pa_xnew(uint8_t, u->in_fragment_size);
                pa_read(u->fd, buf, u->in_fragment_size, NULL);
                pa_xfree(buf);
            }
        }
    }
}

static void mmap_fill_memblocks(struct userdata *u, unsigned n) {
    pa_assert(u);
    pa_assert(u->out_mmap_memblocks);

/*     pa_log("Mmmap writing %u blocks", n); */

    while (n > 0) {
        pa_memchunk chunk;

        if (u->out_mmap_memblocks[u->out_mmap_current])
            pa_memblock_unref_fixed(u->out_mmap_memblocks[u->out_mmap_current]);

        chunk.memblock = u->out_mmap_memblocks[u->out_mmap_current] =
            pa_memblock_new_fixed(
                    u->core->mempool,
                    (uint8_t*) u->out_mmap + u->out_fragment_size * u->out_mmap_current,
                    u->out_fragment_size,
                    1);

        chunk.length = pa_memblock_get_length(chunk.memblock);
        chunk.index = 0;

        pa_sink_render_into_full(u->sink, &chunk);

        u->out_mmap_current++;
        while (u->out_mmap_current >= u->out_nfrags)
            u->out_mmap_current -= u->out_nfrags;

        n--;
    }
}

static int mmap_write(struct userdata *u) {
    struct count_info info;

    pa_assert(u);
    pa_assert(u->sink);

/*     pa_log("Mmmap writing..."); */

    if (ioctl(u->fd, SNDCTL_DSP_GETOPTR, &info) < 0) {
        pa_log("SNDCTL_DSP_GETOPTR: %s", pa_cstrerror(errno));
        return -1;
    }

    info.blocks += u->out_mmap_saved_nfrags;
    u->out_mmap_saved_nfrags = 0;

    if (info.blocks > 0)
        mmap_fill_memblocks(u, info.blocks);

    return info.blocks;
}

static void mmap_post_memblocks(struct userdata *u, unsigned n) {
    pa_assert(u);
    pa_assert(u->in_mmap_memblocks);

/*     pa_log("Mmmap reading %u blocks", n); */

    while (n > 0) {
        pa_memchunk chunk;

        if (!u->in_mmap_memblocks[u->in_mmap_current]) {

            chunk.memblock = u->in_mmap_memblocks[u->in_mmap_current] =
                pa_memblock_new_fixed(
                        u->core->mempool,
                        (uint8_t*) u->in_mmap + u->in_fragment_size*u->in_mmap_current,
                        u->in_fragment_size,
                        1);

            chunk.length = pa_memblock_get_length(chunk.memblock);
            chunk.index = 0;

            pa_source_post(u->source, &chunk);
        }

        u->in_mmap_current++;
        while (u->in_mmap_current >= u->in_nfrags)
            u->in_mmap_current -= u->in_nfrags;

        n--;
    }
}

static void mmap_clear_memblocks(struct userdata*u, unsigned n) {
    unsigned i = u->in_mmap_current;

    pa_assert(u);
    pa_assert(u->in_mmap_memblocks);

    if (n > u->in_nfrags)
        n = u->in_nfrags;

    while (n > 0) {
        if (u->in_mmap_memblocks[i]) {
            pa_memblock_unref_fixed(u->in_mmap_memblocks[i]);
            u->in_mmap_memblocks[i] = NULL;
        }

        i++;
        while (i >= u->in_nfrags)
            i -= u->in_nfrags;

        n--;
    }
}

static int mmap_read(struct userdata *u) {
    struct count_info info;
    pa_assert(u);
    pa_assert(u->source);

/*     pa_log("Mmmap reading..."); */

    if (ioctl(u->fd, SNDCTL_DSP_GETIPTR, &info) < 0) {
        pa_log("SNDCTL_DSP_GETIPTR: %s", pa_cstrerror(errno));
        return -1;
    }

/*     pa_log("... %i", info.blocks); */

    info.blocks += u->in_mmap_saved_nfrags;
    u->in_mmap_saved_nfrags = 0;

    if (info.blocks > 0) {
        mmap_post_memblocks(u, info.blocks);
        mmap_clear_memblocks(u, u->in_nfrags/2);
    }

    return info.blocks;
}

static pa_usec_t mmap_sink_get_latency(struct userdata *u) {
    struct count_info info;
    size_t bpos, n;

    pa_assert(u);

    if (ioctl(u->fd, SNDCTL_DSP_GETOPTR, &info) < 0) {
        pa_log("SNDCTL_DSP_GETOPTR: %s", pa_cstrerror(errno));
        return 0;
    }

    u->out_mmap_saved_nfrags += info.blocks;

    bpos = ((u->out_mmap_current + u->out_mmap_saved_nfrags) * u->out_fragment_size) % u->out_hwbuf_size;

    if (bpos <= (size_t) info.ptr)
        n = u->out_hwbuf_size - (info.ptr - bpos);
    else
        n = bpos - info.ptr;

/*     pa_log("n = %u, bpos = %u, ptr = %u, total=%u, fragsize = %u, n_frags = %u\n", n, bpos, (unsigned) info.ptr, total, u->out_fragment_size, u->out_fragments); */

    return pa_bytes_to_usec(n, &u->sink->sample_spec);
}

static pa_usec_t mmap_source_get_latency(struct userdata *u) {
    struct count_info info;
    size_t bpos, n;

    pa_assert(u);

    if (ioctl(u->fd, SNDCTL_DSP_GETIPTR, &info) < 0) {
        pa_log("SNDCTL_DSP_GETIPTR: %s", pa_cstrerror(errno));
        return 0;
    }

    u->in_mmap_saved_nfrags += info.blocks;
    bpos = ((u->in_mmap_current + u->in_mmap_saved_nfrags) * u->in_fragment_size) % u->in_hwbuf_size;

    if (bpos <= (size_t) info.ptr)
        n = info.ptr - bpos;
    else
        n = u->in_hwbuf_size - bpos + info.ptr;

/*     pa_log("n = %u, bpos = %u, ptr = %u, total=%u, fragsize = %u, n_frags = %u\n", n, bpos, (unsigned) info.ptr, total, u->in_fragment_size, u->in_fragments);  */

    return pa_bytes_to_usec(n, &u->source->sample_spec);
}

static pa_usec_t io_sink_get_latency(struct userdata *u) {
    pa_usec_t r = 0;

    pa_assert(u);

    if (u->use_getodelay) {
        int arg;

        if (ioctl(u->fd, SNDCTL_DSP_GETODELAY, &arg) < 0) {
            pa_log_info("Device doesn't support SNDCTL_DSP_GETODELAY: %s", pa_cstrerror(errno));
            u->use_getodelay = 0;
        } else
            r = pa_bytes_to_usec(arg, &u->sink->sample_spec);

    }

    if (!u->use_getodelay && u->use_getospace) {
        struct audio_buf_info info;

        if (ioctl(u->fd, SNDCTL_DSP_GETOSPACE, &info) < 0) {
            pa_log_info("Device doesn't support SNDCTL_DSP_GETOSPACE: %s", pa_cstrerror(errno));
            u->use_getospace = 0;
        } else
            r = pa_bytes_to_usec(info.bytes, &u->sink->sample_spec);
    }

    if (u->memchunk.memblock)
        r += pa_bytes_to_usec(u->memchunk.length, &u->sink->sample_spec);

    return r;
}


static pa_usec_t io_source_get_latency(struct userdata *u) {
    pa_usec_t r = 0;

    pa_assert(u);

    if (u->use_getispace) {
        struct audio_buf_info info;

        if (ioctl(u->fd, SNDCTL_DSP_GETISPACE, &info) < 0) {
            pa_log_info("Device doesn't support SNDCTL_DSP_GETISPACE: %s", pa_cstrerror(errno));
            u->use_getispace = 0;
        } else
            r = pa_bytes_to_usec(info.bytes, &u->source->sample_spec);
    }

    return r;
}

static void build_pollfd(struct userdata *u) {
    struct pollfd *pollfd;

    pa_assert(u);
    pa_assert(u->fd >= 0);

    if (u->rtpoll_item)
        pa_rtpoll_item_free(u->rtpoll_item);

    u->rtpoll_item = pa_rtpoll_item_new(u->rtpoll, PA_RTPOLL_NEVER, 1);
    pollfd = pa_rtpoll_item_get_pollfd(u->rtpoll_item, NULL);
    pollfd->fd = u->fd;
    pollfd->events = 0;
    pollfd->revents = 0;
}

static int suspend(struct userdata *u) {
    pa_assert(u);
    pa_assert(u->fd >= 0);

    pa_log_info("Suspending...");

    if (u->out_mmap_memblocks) {
        unsigned i;
        for (i = 0; i < u->out_nfrags; i++)
            if (u->out_mmap_memblocks[i]) {
                pa_memblock_unref_fixed(u->out_mmap_memblocks[i]);
                u->out_mmap_memblocks[i] = NULL;
            }
    }

    if (u->in_mmap_memblocks) {
        unsigned i;
        for (i = 0; i < u->in_nfrags; i++)
            if (u->in_mmap_memblocks[i]) {
                pa_memblock_unref_fixed(u->in_mmap_memblocks[i]);
                u->in_mmap_memblocks[i] = NULL;
            }
    }

    if (u->in_mmap && u->in_mmap != MAP_FAILED) {
        munmap(u->in_mmap, u->in_hwbuf_size);
        u->in_mmap = NULL;
    }

    if (u->out_mmap && u->out_mmap != MAP_FAILED) {
        munmap(u->out_mmap, u->out_hwbuf_size);
        u->out_mmap = NULL;
    }

    /* Let's suspend */
    ioctl(u->fd, SNDCTL_DSP_SYNC, NULL);
    pa_close(u->fd);
    u->fd = -1;

    if (u->rtpoll_item) {
        pa_rtpoll_item_free(u->rtpoll_item);
        u->rtpoll_item = NULL;
    }

    pa_log_info("Device suspended...");

    return 0;
}

static int unsuspend(struct userdata *u) {
    int m;
    pa_sample_spec ss, *ss_original;
    int frag_size, in_frag_size, out_frag_size;
    int in_nfrags, out_nfrags;
    struct audio_buf_info info;

    pa_assert(u);
    pa_assert(u->fd < 0);

    m = u->mode;

    pa_log_info("Trying resume...");

    if ((u->fd = pa_oss_open(u->device_name, &m, NULL)) < 0) {
        pa_log_warn("Resume failed, device busy (%s)", pa_cstrerror(errno));
        return -1;

    if (m != u->mode)
        pa_log_warn("Resume failed, couldn't open device with original access mode.");
        goto fail;
    }

    if (u->nfrags >= 2 && u->frag_size >= 1)
        if (pa_oss_set_fragments(u->fd, u->nfrags, u->frag_size) < 0) {
            pa_log_warn("Resume failed, couldn't set original fragment settings.");
            goto fail;
        }

    ss = *(ss_original = u->sink ? &u->sink->sample_spec : &u->source->sample_spec);
    if (pa_oss_auto_format(u->fd, &ss) < 0 || !pa_sample_spec_equal(&ss, ss_original)) {
        pa_log_warn("Resume failed, couldn't set original sample format settings.");
        goto fail;
    }

    if (ioctl(u->fd, SNDCTL_DSP_GETBLKSIZE, &frag_size) < 0) {
        pa_log_warn("SNDCTL_DSP_GETBLKSIZE: %s", pa_cstrerror(errno));
        goto fail;
    }

    in_frag_size = out_frag_size = frag_size;
    in_nfrags = out_nfrags = u->nfrags;

    if (ioctl(u->fd, SNDCTL_DSP_GETISPACE, &info) >= 0) {
        in_frag_size = info.fragsize;
        in_nfrags = info.fragstotal;
    }

    if (ioctl(u->fd, SNDCTL_DSP_GETOSPACE, &info) >= 0) {
        out_frag_size = info.fragsize;
        out_nfrags = info.fragstotal;
    }

    if ((u->source && (in_frag_size != (int) u->in_fragment_size || in_nfrags != (int) u->in_nfrags)) ||
        (u->sink && (out_frag_size != (int) u->out_fragment_size || out_nfrags != (int) u->out_nfrags))) {
        pa_log_warn("Resume failed, input fragment settings don't match.");
        goto fail;
    }

    if (u->use_mmap) {
        if (u->source) {
            if ((u->in_mmap = mmap(NULL, u->in_hwbuf_size, PROT_READ, MAP_SHARED, u->fd, 0)) == MAP_FAILED) {
                pa_log("Resume failed, mmap(): %s", pa_cstrerror(errno));
                goto fail;
            }
        }

        if (u->sink) {
            if ((u->out_mmap = mmap(NULL, u->out_hwbuf_size, PROT_WRITE, MAP_SHARED, u->fd, 0)) == MAP_FAILED) {
                pa_log("Resume failed, mmap(): %s", pa_cstrerror(errno));
                if (u->in_mmap && u->in_mmap != MAP_FAILED) {
                    munmap(u->in_mmap, u->in_hwbuf_size);
                    u->in_mmap = NULL;
                }

                goto fail;
            }

            pa_silence_memory(u->out_mmap, u->out_hwbuf_size, &ss);
        }
    }

    u->out_mmap_current = u->in_mmap_current = 0;
    u->out_mmap_saved_nfrags = u->in_mmap_saved_nfrags = 0;

    pa_assert(!u->rtpoll_item);

    build_pollfd(u);

    if (u->sink)
        pa_sink_get_volume(u->sink);
    if (u->source)
        pa_source_get_volume(u->source);

    pa_log_info("Resumed successfully...");

    return 0;

fail:
    pa_close(u->fd);
    u->fd = -1;
    return -1;
}

static int sink_process_msg(pa_msgobject *o, int code, void *data, int64_t offset, pa_memchunk *chunk) {
    struct userdata *u = PA_SINK(o)->userdata;
    int ret;
    pa_bool_t do_trigger = FALSE, quick = TRUE;

    switch (code) {

        case PA_SINK_MESSAGE_GET_LATENCY: {
            pa_usec_t r = 0;

            if (u->fd >= 0) {
                if (u->use_mmap)
                    r = mmap_sink_get_latency(u);
                else
                    r = io_sink_get_latency(u);
            }

            *((pa_usec_t*) data) = r;

            return 0;
        }

        case PA_SINK_MESSAGE_SET_STATE:

            switch ((pa_sink_state_t) PA_PTR_TO_UINT(data)) {

                case PA_SINK_SUSPENDED:
                    pa_assert(PA_SINK_OPENED(u->sink->thread_info.state));

                    if (!u->source || u->source_suspended) {
                        if (suspend(u) < 0)
                            return -1;
                    }

                    do_trigger = TRUE;

                    u->sink_suspended = TRUE;
                    break;

                case PA_SINK_IDLE:
                case PA_SINK_RUNNING:

                    if (u->sink->thread_info.state == PA_SINK_INIT) {
                        do_trigger = TRUE;
                        quick = u->source && PA_SOURCE_OPENED(u->source->thread_info.state);
                    }

                    if (u->sink->thread_info.state == PA_SINK_SUSPENDED) {

                        if (!u->source || u->source_suspended) {
                            if (unsuspend(u) < 0)
                                return -1;
                            quick = FALSE;
                        }

                        do_trigger = TRUE;

                        u->out_mmap_current = 0;
                        u->out_mmap_saved_nfrags = 0;

                        u->sink_suspended = FALSE;
                    }

                    break;

                case PA_SINK_UNLINKED:
                case PA_SINK_INIT:
                    ;
            }

            break;

    }

    ret = pa_sink_process_msg(o, code, data, offset, chunk);

    if (do_trigger)
        trigger(u, quick);

    return ret;
}

static int source_process_msg(pa_msgobject *o, int code, void *data, int64_t offset, pa_memchunk *chunk) {
    struct userdata *u = PA_SOURCE(o)->userdata;
    int ret;
    int do_trigger = FALSE, quick = TRUE;

    switch (code) {

        case PA_SOURCE_MESSAGE_GET_LATENCY: {
            pa_usec_t r = 0;

            if (u->fd >= 0) {
                if (u->use_mmap)
                    r = mmap_source_get_latency(u);
                else
                    r = io_source_get_latency(u);
            }

            *((pa_usec_t*) data) = r;
            return 0;
        }

        case PA_SOURCE_MESSAGE_SET_STATE:

            switch ((pa_source_state_t) PA_PTR_TO_UINT(data)) {
                case PA_SOURCE_SUSPENDED:
                    pa_assert(PA_SOURCE_OPENED(u->source->thread_info.state));

                    if (!u->sink || u->sink_suspended) {
                        if (suspend(u) < 0)
                            return -1;
                    }

                    do_trigger = TRUE;

                    u->source_suspended = TRUE;
                    break;

                case PA_SOURCE_IDLE:
                case PA_SOURCE_RUNNING:

                    if (u->source->thread_info.state == PA_SOURCE_INIT) {
                        do_trigger = TRUE;
                        quick = u->sink && PA_SINK_OPENED(u->sink->thread_info.state);
                    }

                    if (u->source->thread_info.state == PA_SOURCE_SUSPENDED) {

                        if (!u->sink || u->sink_suspended) {
                            if (unsuspend(u) < 0)
                                return -1;
                            quick = FALSE;
                        }

                        do_trigger = TRUE;

                        u->in_mmap_current = 0;
                        u->in_mmap_saved_nfrags = 0;

                        u->source_suspended = FALSE;
                    }
                    break;

                case PA_SOURCE_UNLINKED:
                case PA_SOURCE_INIT:
                    ;

            }
            break;

    }

    ret = pa_source_process_msg(o, code, data, offset, chunk);

    if (do_trigger)
        trigger(u, quick);

    return ret;
}

static int sink_get_volume(pa_sink *s) {
    struct userdata *u;
    int r;

    pa_assert_se(u = s->userdata);

    pa_assert(u->mixer_devmask & (SOUND_MASK_VOLUME|SOUND_MASK_PCM));

    if (u->mixer_devmask & SOUND_MASK_VOLUME)
        if ((r = pa_oss_get_volume(u->mixer_fd, SOUND_MIXER_READ_VOLUME, &s->sample_spec, &s->volume)) >= 0)
            return r;

    if (u->mixer_devmask & SOUND_MASK_PCM)
        if ((r = pa_oss_get_volume(u->mixer_fd, SOUND_MIXER_READ_PCM, &s->sample_spec, &s->volume)) >= 0)
            return r;

    pa_log_info("Device doesn't support reading mixer settings: %s", pa_cstrerror(errno));
    return -1;
}

static int sink_set_volume(pa_sink *s) {
    struct userdata *u;
    int r;

    pa_assert_se(u = s->userdata);

    pa_assert(u->mixer_devmask & (SOUND_MASK_VOLUME|SOUND_MASK_PCM));

    if (u->mixer_devmask & SOUND_MASK_VOLUME)
        if ((r = pa_oss_set_volume(u->mixer_fd, SOUND_MIXER_WRITE_VOLUME, &s->sample_spec, &s->volume)) >= 0)
            return r;

    if (u->mixer_devmask & SOUND_MASK_PCM)
        if ((r = pa_oss_get_volume(u->mixer_fd, SOUND_MIXER_WRITE_PCM, &s->sample_spec, &s->volume)) >= 0)
            return r;

    pa_log_info("Device doesn't support writing mixer settings: %s", pa_cstrerror(errno));
    return -1;
}

static int source_get_volume(pa_source *s) {
    struct userdata *u;
    int r;

    pa_assert_se(u = s->userdata);

    pa_assert(u->mixer_devmask & (SOUND_MASK_IGAIN|SOUND_MASK_RECLEV));

    if (u->mixer_devmask & SOUND_MASK_IGAIN)
        if ((r = pa_oss_get_volume(u->mixer_fd, SOUND_MIXER_READ_IGAIN, &s->sample_spec, &s->volume)) >= 0)
            return r;

    if (u->mixer_devmask & SOUND_MASK_RECLEV)
        if ((r = pa_oss_get_volume(u->mixer_fd, SOUND_MIXER_READ_RECLEV, &s->sample_spec, &s->volume)) >= 0)
            return r;

    pa_log_info("Device doesn't support reading mixer settings: %s", pa_cstrerror(errno));
    return -1;
}

static int source_set_volume(pa_source *s) {
    struct userdata *u;
    int r;

    pa_assert_se(u = s->userdata);

    pa_assert(u->mixer_devmask & (SOUND_MASK_IGAIN|SOUND_MASK_RECLEV));

    if (u->mixer_devmask & SOUND_MASK_IGAIN)
        if ((r = pa_oss_set_volume(u->mixer_fd, SOUND_MIXER_WRITE_IGAIN, &s->sample_spec, &s->volume)) >= 0)
            return r;

    if (u->mixer_devmask & SOUND_MASK_RECLEV)
        if ((r = pa_oss_get_volume(u->mixer_fd, SOUND_MIXER_WRITE_RECLEV, &s->sample_spec, &s->volume)) >= 0)
            return r;

    pa_log_info("Device doesn't support writing mixer settings: %s", pa_cstrerror(errno));
    return -1;
}

static void thread_func(void *userdata) {
    struct userdata *u = userdata;
    int write_type = 0, read_type = 0;
    unsigned short revents = 0;

    pa_assert(u);

    pa_log_debug("Thread starting up");

    if (u->core->realtime_scheduling)
        pa_make_realtime(u->core->realtime_priority);

    pa_thread_mq_install(&u->thread_mq);
    pa_rtpoll_install(u->rtpoll);

    for (;;) {
        int ret;

/*        pa_log("loop");    */

        /* Render some data and write it to the dsp */

        if (u->sink && PA_SINK_OPENED(u->sink->thread_info.state) && ((revents & POLLOUT) || u->use_mmap || u->use_getospace)) {

            if (u->use_mmap) {

                if ((ret = mmap_write(u)) < 0)
                    goto fail;

                revents &= ~POLLOUT;

                if (ret > 0)
                    continue;

            } else {
                ssize_t l;
                pa_bool_t loop = FALSE, work_done = FALSE;

                l = u->out_fragment_size;

                if (u->use_getospace) {
                    audio_buf_info info;

                    if (ioctl(u->fd, SNDCTL_DSP_GETOSPACE, &info) < 0) {
                        pa_log_info("Device doesn't support SNDCTL_DSP_GETOSPACE: %s", pa_cstrerror(errno));
                        u->use_getospace = FALSE;
                    } else {
                        l = info.bytes;

                        /* We loop only if GETOSPACE worked and we
                         * actually *know* that we can write more than
                         * one fragment at a time */
                        loop = TRUE;
                    }
                }

                /* Round down to multiples of the fragment size,
                 * because OSS needs that (at least some versions
                 * do) */
                l = (l/u->out_fragment_size) * u->out_fragment_size;

                /* Hmm, so poll() signalled us that we can read
                 * something, but GETOSPACE told us there was nothing?
                 * Hmm, make the best of it, try to read some data, to
                 * avoid spinning forever. */
                if (l <= 0 && (revents & POLLOUT)) {
                    l = u->out_fragment_size;
                    loop = FALSE;
                }

                while (l > 0) {
                    void *p;
                    ssize_t t;

                    if (u->memchunk.length <= 0)
                        pa_sink_render(u->sink, l, &u->memchunk);

                    pa_assert(u->memchunk.length > 0);

                    p = pa_memblock_acquire(u->memchunk.memblock);
                    t = pa_write(u->fd, (uint8_t*) p + u->memchunk.index, u->memchunk.length, &write_type);
                    pa_memblock_release(u->memchunk.memblock);

/*                     pa_log("wrote %i bytes of %u", t, l); */

                    pa_assert(t != 0);

                    if (t < 0) {

                        if (errno == EINTR)
                            continue;

                        else if (errno == EAGAIN) {
                            pa_log_debug("EAGAIN");

                            revents &= ~POLLOUT;
                            break;

                        } else {
                            pa_log("Failed to write data to DSP: %s", pa_cstrerror(errno));
                            goto fail;
                        }

                    } else {

                        u->memchunk.index += t;
                        u->memchunk.length -= t;

                        if (u->memchunk.length <= 0) {
                            pa_memblock_unref(u->memchunk.memblock);
                            pa_memchunk_reset(&u->memchunk);
                        }

                        l -= t;

                        revents &= ~POLLOUT;
                        work_done = TRUE;
                    }

                    if (!loop)
                        break;
                }

                if (work_done)
                    continue;
            }
        }

        /* Try to read some data and pass it on to the source driver. */

        if (u->source && PA_SOURCE_OPENED(u->source->thread_info.state) && ((revents & POLLIN) || u->use_mmap || u->use_getispace)) {

            if (u->use_mmap) {

                if ((ret = mmap_read(u)) < 0)
                    goto fail;

                revents &= ~POLLIN;

                if (ret > 0)
                    continue;

            } else {

                void *p;
                ssize_t l;
                pa_memchunk memchunk;
                pa_bool_t loop = FALSE, work_done = FALSE;

                l = u->in_fragment_size;

                if (u->use_getispace) {
                    audio_buf_info info;

                    if (ioctl(u->fd, SNDCTL_DSP_GETISPACE, &info) < 0) {
                        pa_log_info("Device doesn't support SNDCTL_DSP_GETISPACE: %s", pa_cstrerror(errno));
                        u->use_getispace = FALSE;
                    } else {
                        l = info.bytes;
                        loop = TRUE;
                    }
                }

                l = (l/u->in_fragment_size) * u->in_fragment_size;

                if (l <= 0 && (revents & POLLIN)) {
                    l = u->in_fragment_size;
                    loop = FALSE;
                }

                while (l > 0) {
                    ssize_t t, k;

                    pa_assert(l > 0);

                    memchunk.memblock = pa_memblock_new(u->core->mempool, (size_t) -1);

                    k = pa_memblock_get_length(memchunk.memblock);

                    if (k > l)
                        k = l;

                    k = (k/u->frame_size)*u->frame_size;

                    p = pa_memblock_acquire(memchunk.memblock);
                    t = pa_read(u->fd, p, k, &read_type);
                    pa_memblock_release(memchunk.memblock);

                    pa_assert(t != 0); /* EOF cannot happen */

/*                     pa_log("read %i bytes of %u", t, l); */

                    if (t < 0) {
                        pa_memblock_unref(memchunk.memblock);

                        if (errno == EINTR)
                            continue;

                        else if (errno == EAGAIN) {
                            pa_log_debug("EAGAIN");

                            revents &= ~POLLIN;
                            break;

                        } else {
                            pa_log("Failed to read data from DSP: %s", pa_cstrerror(errno));
                            goto fail;
                        }

                    } else {
                        memchunk.index = 0;
                        memchunk.length = t;

                        pa_source_post(u->source, &memchunk);
                        pa_memblock_unref(memchunk.memblock);

                        l -= t;

                        revents &= ~POLLIN;
                        work_done = TRUE;
                    }

                    if (!loop)
                        break;
                }

                if (work_done)
                    continue;
            }
        }

/*         pa_log("loop2 revents=%i", revents); */

        if (u->rtpoll_item) {
            struct pollfd *pollfd;

            pa_assert(u->fd >= 0);

            pollfd = pa_rtpoll_item_get_pollfd(u->rtpoll_item, NULL);
            pollfd->events =
                ((u->source && PA_SOURCE_OPENED(u->source->thread_info.state)) ? POLLIN : 0) |
                ((u->sink && PA_SINK_OPENED(u->sink->thread_info.state)) ? POLLOUT : 0);
        }

        /* Hmm, nothing to do. Let's sleep */
        if ((ret = pa_rtpoll_run(u->rtpoll, TRUE)) < 0)
            goto fail;

        if (ret == 0)
            goto finish;

        if (u->rtpoll_item) {
            struct pollfd *pollfd;

            pollfd = pa_rtpoll_item_get_pollfd(u->rtpoll_item, NULL);

            if (pollfd->revents & ~(POLLOUT|POLLIN)) {
                pa_log("DSP shutdown.");
                goto fail;
            }

            revents = pollfd->revents;
        } else
            revents = 0;
    }

fail:
    /* If this was no regular exit from the loop we have to continue
     * processing messages until we received PA_MESSAGE_SHUTDOWN */
    pa_asyncmsgq_post(u->thread_mq.outq, PA_MSGOBJECT(u->core), PA_CORE_MESSAGE_UNLOAD_MODULE, u->module, 0, NULL, NULL);
    pa_asyncmsgq_wait_for(u->thread_mq.inq, PA_MESSAGE_SHUTDOWN);

finish:
    pa_log_debug("Thread shutting down");
}

int pa__init(pa_module*m) {

    struct audio_buf_info info;
    struct userdata *u = NULL;
    const char *dev;
    int fd = -1;
    int nfrags, frag_size;
    int mode, caps;
    pa_bool_t record = TRUE, playback = TRUE, use_mmap = TRUE;
    pa_sample_spec ss;
    pa_channel_map map;
    pa_modargs *ma = NULL;
    char hwdesc[64], *t;
    const char *name;
    int namereg_fail;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments.");
        goto fail;
    }

    if (pa_modargs_get_value_boolean(ma, "record", &record) < 0 || pa_modargs_get_value_boolean(ma, "playback", &playback) < 0) {
        pa_log("record= and playback= expect boolean argument.");
        goto fail;
    }

    if (!playback && !record) {
        pa_log("Neither playback nor record enabled for device.");
        goto fail;
    }

    mode = (playback && record) ? O_RDWR : (playback ? O_WRONLY : (record ? O_RDONLY : 0));

    ss = m->core->default_sample_spec;
    if (pa_modargs_get_sample_spec_and_channel_map(ma, &ss, &map, PA_CHANNEL_MAP_OSS) < 0) {
        pa_log("Failed to parse sample specification or channel map");
        goto fail;
    }

    nfrags = m->core->default_n_fragments;
    frag_size = pa_usec_to_bytes(m->core->default_fragment_size_msec*1000, &ss);
    if (frag_size <= 0)
        frag_size = pa_frame_size(&ss);

    if (pa_modargs_get_value_s32(ma, "fragments", &nfrags) < 0 || pa_modargs_get_value_s32(ma, "fragment_size", &frag_size) < 0) {
        pa_log("Failed to parse fragments arguments");
        goto fail;
    }

    if (pa_modargs_get_value_boolean(ma, "mmap", &use_mmap) < 0) {
        pa_log("Failed to parse mmap argument.");
        goto fail;
    }

    if ((fd = pa_oss_open(dev = pa_modargs_get_value(ma, "device", DEFAULT_DEVICE), &mode, &caps)) < 0)
        goto fail;

    if (use_mmap && (!(caps & DSP_CAP_MMAP) || !(caps & DSP_CAP_TRIGGER))) {
        pa_log_info("OSS device not mmap capable, falling back to UNIX read/write mode.");
        use_mmap = 0;
    }

    if (use_mmap && mode == O_WRONLY) {
        pa_log_info("Device opened for playback only, cannot do memory mapping, falling back to UNIX write() mode.");
        use_mmap = 0;
    }

    if (pa_oss_get_hw_description(dev, hwdesc, sizeof(hwdesc)) >= 0)
        pa_log_info("Hardware name is '%s'.", hwdesc);
    else
        hwdesc[0] = 0;

    pa_log_info("Device opened in %s mode.", mode == O_WRONLY ? "O_WRONLY" : (mode == O_RDONLY ? "O_RDONLY" : "O_RDWR"));

    if (nfrags >= 2 && frag_size >= 1)
        if (pa_oss_set_fragments(fd, nfrags, frag_size) < 0)
            goto fail;

    if (pa_oss_auto_format(fd, &ss) < 0)
        goto fail;

    if (ioctl(fd, SNDCTL_DSP_GETBLKSIZE, &frag_size) < 0) {
        pa_log("SNDCTL_DSP_GETBLKSIZE: %s", pa_cstrerror(errno));
        goto fail;
    }
    pa_assert(frag_size > 0);

    u = pa_xnew0(struct userdata, 1);
    u->core = m->core;
    u->module = m;
    m->userdata = u;
    u->fd = fd;
    u->mixer_fd = -1;
    u->use_getospace = u->use_getispace = 1;
    u->use_getodelay = 1;
    u->mode = mode;
    u->frame_size = pa_frame_size(&ss);
    u->device_name = pa_xstrdup(dev);
    u->in_nfrags = u->out_nfrags = u->nfrags = nfrags;
    u->out_fragment_size = u->in_fragment_size = u->frag_size = frag_size;
    u->use_mmap = use_mmap;
    pa_thread_mq_init(&u->thread_mq, m->core->mainloop);
    u->rtpoll = pa_rtpoll_new();
    pa_rtpoll_item_new_asyncmsgq(u->rtpoll, PA_RTPOLL_EARLY, u->thread_mq.inq);
    u->rtpoll_item = NULL;
    build_pollfd(u);

    if (ioctl(fd, SNDCTL_DSP_GETISPACE, &info) >= 0) {
        pa_log_info("Input -- %u fragments of size %u.", info.fragstotal, info.fragsize);
        u->in_fragment_size = info.fragsize;
        u->in_nfrags = info.fragstotal;
        u->use_getispace = 1;
    }

    if (ioctl(fd, SNDCTL_DSP_GETOSPACE, &info) >= 0) {
        pa_log_info("Output -- %u fragments of size %u.", info.fragstotal, info.fragsize);
        u->out_fragment_size = info.fragsize;
        u->out_nfrags = info.fragstotal;
        u->use_getospace = 1;
    }

    u->in_hwbuf_size = u->in_nfrags * u->in_fragment_size;
    u->out_hwbuf_size = u->out_nfrags * u->out_fragment_size;

    if (mode != O_WRONLY) {
        char *name_buf = NULL;

        if (use_mmap) {
            if ((u->in_mmap = mmap(NULL, u->in_hwbuf_size, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED) {
                pa_log_warn("mmap(PROT_READ) failed, reverting to non-mmap mode: %s", pa_cstrerror(errno));
                use_mmap = u->use_mmap = 0;
                u->in_mmap = NULL;
            } else
                pa_log_debug("Successfully mmap()ed input buffer.");
        }

        if ((name = pa_modargs_get_value(ma, "source_name", NULL)))
            namereg_fail = 1;
        else {
            name = name_buf = pa_sprintf_malloc("oss_input.%s", pa_path_get_filename(dev));
            namereg_fail = 0;
        }

        u->source = pa_source_new(m->core, __FILE__, name, namereg_fail, &ss, &map);
        pa_xfree(name_buf);
        if (!u->source) {
            pa_log("Failed to create source object");
            goto fail;
        }

        u->source->parent.process_msg = source_process_msg;
        u->source->userdata = u;

        pa_source_set_module(u->source, m);
        pa_source_set_asyncmsgq(u->source, u->thread_mq.inq);
        pa_source_set_rtpoll(u->source, u->rtpoll);
        pa_source_set_description(u->source, t = pa_sprintf_malloc(
                                          "OSS PCM on %s%s%s%s%s",
                                          dev,
                                          hwdesc[0] ? " (" : "",
                                          hwdesc[0] ? hwdesc : "",
                                          hwdesc[0] ? ")" : "",
                                          use_mmap ? " via DMA" : ""));
        pa_xfree(t);
        u->source->flags = PA_SOURCE_HARDWARE|PA_SOURCE_LATENCY;
        u->source->refresh_volume = TRUE;

        if (use_mmap)
            u->in_mmap_memblocks = pa_xnew0(pa_memblock*, u->in_nfrags);
    }

    if (mode != O_RDONLY) {
        char *name_buf = NULL;

        if (use_mmap) {
            if ((u->out_mmap = mmap(NULL, u->out_hwbuf_size, PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
                if (mode == O_RDWR) {
                    pa_log_debug("mmap() failed for input. Changing to O_WRONLY mode.");
                    mode = O_WRONLY;
                    goto go_on;
                } else {
                    pa_log_warn("mmap(PROT_WRITE) failed, reverting to non-mmap mode: %s", pa_cstrerror(errno));
                    u->use_mmap = (use_mmap = FALSE);
                    u->out_mmap = NULL;
                }
            } else {
                pa_log_debug("Successfully mmap()ed output buffer.");
                pa_silence_memory(u->out_mmap, u->out_hwbuf_size, &ss);
            }
        }

        if ((name = pa_modargs_get_value(ma, "sink_name", NULL)))
            namereg_fail = 1;
        else {
            name = name_buf = pa_sprintf_malloc("oss_output.%s", pa_path_get_filename(dev));
            namereg_fail = 0;
        }

        u->sink = pa_sink_new(m->core, __FILE__, name, namereg_fail, &ss, &map);
        pa_xfree(name_buf);
        if (!u->sink) {
            pa_log("Failed to create sink object");
            goto fail;
        }

        u->sink->parent.process_msg = sink_process_msg;
        u->sink->userdata = u;

        pa_sink_set_module(u->sink, m);
        pa_sink_set_asyncmsgq(u->sink, u->thread_mq.inq);
        pa_sink_set_rtpoll(u->sink, u->rtpoll);
        pa_sink_set_description(u->sink, t = pa_sprintf_malloc(
                                        "OSS PCM on %s%s%s%s%s",
                                        dev,
                                        hwdesc[0] ? " (" : "",
                                        hwdesc[0] ? hwdesc : "",
                                        hwdesc[0] ? ")" : "",
                                        use_mmap ? " via DMA" : ""));
        pa_xfree(t);
        u->sink->flags = PA_SINK_HARDWARE|PA_SINK_LATENCY;
        u->sink->refresh_volume = TRUE;

        if (use_mmap)
            u->out_mmap_memblocks = pa_xnew0(pa_memblock*, u->out_nfrags);
    }

    if ((u->mixer_fd = pa_oss_open_mixer_for_device(u->device_name)) >= 0) {
        int do_close = 1;
        u->mixer_devmask = 0;

        if (ioctl(fd, SOUND_MIXER_READ_DEVMASK, &u->mixer_devmask) < 0)
            pa_log_warn("SOUND_MIXER_READ_DEVMASK failed: %s", pa_cstrerror(errno));

        else {
            if (u->sink && (u->mixer_devmask & (SOUND_MASK_VOLUME|SOUND_MASK_PCM))) {
                pa_log_debug("Found hardware mixer track for playback.");
                u->sink->flags |= PA_SINK_HW_VOLUME_CTRL;
                u->sink->get_volume = sink_get_volume;
                u->sink->set_volume = sink_set_volume;
                do_close = 0;
            }

            if (u->source && (u->mixer_devmask & (SOUND_MASK_RECLEV|SOUND_MASK_IGAIN))) {
                pa_log_debug("Found hardware mixer track for recording.");
                u->source->flags |= PA_SOURCE_HW_VOLUME_CTRL;
                u->source->get_volume = source_get_volume;
                u->source->set_volume = source_set_volume;
                do_close = 0;
            }
        }

        if (do_close) {
            pa_close(u->mixer_fd);
            u->mixer_fd = -1;
        }
    }

go_on:

    pa_assert(u->source || u->sink);

    pa_memchunk_reset(&u->memchunk);

    if (!(u->thread = pa_thread_new(thread_func, u))) {
        pa_log("Failed to create thread.");
        goto fail;
    }

    /* Read mixer settings */
    if (u->sink && u->sink->get_volume)
        sink_get_volume(u->sink);
    if (u->source && u->source->get_volume)
        source_get_volume(u->source);

    if (u->sink)
        pa_sink_put(u->sink);
    if (u->source)
        pa_source_put(u->source);

    pa_modargs_free(ma);

    return 0;

fail:

    if (u)
        pa__done(m);
    else if (fd >= 0)
        pa_close(fd);

    if (ma)
        pa_modargs_free(ma);

    return -1;
}

void pa__done(pa_module*m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->sink)
        pa_sink_unlink(u->sink);

    if (u->source)
        pa_source_unlink(u->source);

    if (u->thread) {
        pa_asyncmsgq_send(u->thread_mq.inq, NULL, PA_MESSAGE_SHUTDOWN, NULL, 0, NULL);
        pa_thread_free(u->thread);
    }

    pa_thread_mq_done(&u->thread_mq);

    if (u->sink)
        pa_sink_unref(u->sink);

    if (u->source)
        pa_source_unref(u->source);

    if (u->memchunk.memblock)
        pa_memblock_unref(u->memchunk.memblock);

    if (u->rtpoll_item)
        pa_rtpoll_item_free(u->rtpoll_item);

    if (u->rtpoll)
        pa_rtpoll_free(u->rtpoll);

    if (u->out_mmap_memblocks) {
        unsigned i;
        for (i = 0; i < u->out_nfrags; i++)
            if (u->out_mmap_memblocks[i])
                pa_memblock_unref_fixed(u->out_mmap_memblocks[i]);
        pa_xfree(u->out_mmap_memblocks);
    }

    if (u->in_mmap_memblocks) {
        unsigned i;
        for (i = 0; i < u->in_nfrags; i++)
            if (u->in_mmap_memblocks[i])
                pa_memblock_unref_fixed(u->in_mmap_memblocks[i]);
        pa_xfree(u->in_mmap_memblocks);
    }

    if (u->in_mmap && u->in_mmap != MAP_FAILED)
        munmap(u->in_mmap, u->in_hwbuf_size);

    if (u->out_mmap && u->out_mmap != MAP_FAILED)
        munmap(u->out_mmap, u->out_hwbuf_size);

    if (u->fd >= 0)
        pa_close(u->fd);

    if (u->mixer_fd >= 0)
        pa_close(u->mixer_fd);

    pa_xfree(u->device_name);

    pa_xfree(u);
}
