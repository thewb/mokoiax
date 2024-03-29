/* $Id: module-esound-sink.c 2043 2007-11-09 18:25:40Z lennart $ */

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

#include <stdlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>

#ifdef HAVE_LINUX_SOCKIOS_H
#include <linux/sockios.h>
#endif

#include <pulse/xmalloc.h>
#include <pulse/timeval.h>

#include <pulsecore/core-error.h>
#include <pulsecore/iochannel.h>
#include <pulsecore/sink.h>
#include <pulsecore/module.h>
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>
#include <pulsecore/socket-client.h>
#include <pulsecore/esound.h>
#include <pulsecore/authkey.h>
#include <pulsecore/thread-mq.h>
#include <pulsecore/thread.h>
#include <pulsecore/time-smoother.h>
#include <pulsecore/rtclock.h>
#include <pulsecore/socket-util.h>

#include "module-esound-sink-symdef.h"

PA_MODULE_AUTHOR("Lennart Poettering");
PA_MODULE_DESCRIPTION("ESOUND Sink");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(FALSE);
PA_MODULE_USAGE(
        "sink_name=<name for the sink> "
        "server=<address> cookie=<filename>  "
        "format=<sample format> "
        "channels=<number of channels> "
        "rate=<sample rate>");

#define DEFAULT_SINK_NAME "esound_out"

struct userdata {
    pa_core *core;
    pa_module *module;
    pa_sink *sink;

    pa_thread_mq thread_mq;
    pa_rtpoll *rtpoll;
    pa_rtpoll_item *rtpoll_item;
    pa_thread *thread;

    pa_memchunk memchunk;

    void *write_data;
    size_t write_length, write_index;

    void *read_data;
    size_t read_length, read_index;

    enum {
        STATE_AUTH,
        STATE_LATENCY,
        STATE_PREPARE,
        STATE_RUNNING,
        STATE_DEAD
    } state;

    pa_usec_t latency;

    esd_format_t format;
    int32_t rate;

    pa_smoother *smoother;
    int fd;

    int64_t offset;

    pa_iochannel *io;
    pa_socket_client *client;

    size_t block_size;
};

static const char* const valid_modargs[] = {
    "server",
    "cookie",
    "rate",
    "format",
    "channels",
    "sink_name",
    NULL
};

enum {
    SINK_MESSAGE_PASS_SOCKET = PA_SINK_MESSAGE_MAX
};

static int sink_process_msg(pa_msgobject *o, int code, void *data, int64_t offset, pa_memchunk *chunk) {
    struct userdata *u = PA_SINK(o)->userdata;

    switch (code) {

        case PA_SINK_MESSAGE_SET_STATE:

            switch ((pa_sink_state_t) PA_PTR_TO_UINT(data)) {

                case PA_SINK_SUSPENDED:
                    pa_assert(PA_SINK_OPENED(u->sink->thread_info.state));

                    pa_smoother_pause(u->smoother, pa_rtclock_usec());
                    break;

                case PA_SINK_IDLE:
                case PA_SINK_RUNNING:

                    if (u->sink->thread_info.state == PA_SINK_SUSPENDED)
                        pa_smoother_resume(u->smoother, pa_rtclock_usec());

                    break;

                case PA_SINK_UNLINKED:
                case PA_SINK_INIT:
                    ;
            }

            break;

        case PA_SINK_MESSAGE_GET_LATENCY: {
            pa_usec_t w, r;

            r = pa_smoother_get(u->smoother, pa_rtclock_usec());
            w = pa_bytes_to_usec(u->offset + u->memchunk.length, &u->sink->sample_spec);

            *((pa_usec_t*) data) = w > r ? w - r : 0;
            break;
        }

        case SINK_MESSAGE_PASS_SOCKET: {
            struct pollfd *pollfd;

            pa_assert(!u->rtpoll_item);

            u->rtpoll_item = pa_rtpoll_item_new(u->rtpoll, PA_RTPOLL_NEVER, 1);
            pollfd = pa_rtpoll_item_get_pollfd(u->rtpoll_item, NULL);
            pollfd->fd = u->fd;
            pollfd->events = pollfd->revents = 0;

            return 0;
        }
    }

    return pa_sink_process_msg(o, code, data, offset, chunk);
}

static void thread_func(void *userdata) {
    struct userdata *u = userdata;
    int write_type = 0;

    pa_assert(u);

    pa_log_debug("Thread starting up");

    pa_thread_mq_install(&u->thread_mq);
    pa_rtpoll_install(u->rtpoll);

    pa_smoother_set_time_offset(u->smoother, pa_rtclock_usec());

    for (;;) {
        int ret;

        if (u->rtpoll_item) {
            struct pollfd *pollfd;
            pollfd = pa_rtpoll_item_get_pollfd(u->rtpoll_item, NULL);

            /* Render some data and write it to the fifo */
            if (PA_SINK_OPENED(u->sink->thread_info.state) && pollfd->revents) {
                pa_usec_t usec;
                int64_t n;

                for (;;) {
                    ssize_t l;
                    void *p;

                    if (u->memchunk.length <= 0)
                        pa_sink_render(u->sink, u->block_size, &u->memchunk);

                    pa_assert(u->memchunk.length > 0);

                    p = pa_memblock_acquire(u->memchunk.memblock);
                    l = pa_write(u->fd, (uint8_t*) p + u->memchunk.index, u->memchunk.length, &write_type);
                    pa_memblock_release(u->memchunk.memblock);

                    pa_assert(l != 0);

                    if (l < 0) {

                        if (errno == EINTR)
                            continue;
                        else if (errno == EAGAIN) {

                            /* OK, we filled all socket buffers up
                             * now. */
                            goto filled_up;

                        } else {
                            pa_log("Failed to write data to FIFO: %s", pa_cstrerror(errno));
                            goto fail;
                        }

                    } else {
                        u->offset += l;

                        u->memchunk.index += l;
                        u->memchunk.length -= l;

                        if (u->memchunk.length <= 0) {
                            pa_memblock_unref(u->memchunk.memblock);
                            pa_memchunk_reset(&u->memchunk);
                        }

                        pollfd->revents = 0;

                        if (u->memchunk.length > 0)

                            /* OK, we wrote less that we asked for,
                             * hence we can assume that the socket
                             * buffers are full now */
                            goto filled_up;
                    }
                }

            filled_up:

                /* At this spot we know that the socket buffers are
                 * fully filled up. This is the best time to estimate
                 * the playback position of the server */

                n = u->offset;

#ifdef SIOCOUTQ
                {
                    int l;
                    if (ioctl(u->fd, SIOCOUTQ, &l) >= 0 && l > 0)
                        n -= l;
                }
#endif

                usec = pa_bytes_to_usec(n, &u->sink->sample_spec);

                if (usec > u->latency)
                    usec -= u->latency;
                else
                    usec = 0;

                pa_smoother_put(u->smoother, pa_rtclock_usec(), usec);
            }

            /* Hmm, nothing to do. Let's sleep */
            pollfd->events = PA_SINK_OPENED(u->sink->thread_info.state)  ? POLLOUT : 0;
        }

        if ((ret = pa_rtpoll_run(u->rtpoll, TRUE)) < 0)
            goto fail;

        if (ret == 0)
            goto finish;

        if (u->rtpoll_item) {
            struct pollfd* pollfd;

            pollfd = pa_rtpoll_item_get_pollfd(u->rtpoll_item, NULL);

            if (pollfd->revents & ~POLLOUT) {
                pa_log("FIFO shutdown.");
                goto fail;
            }
        }
    }

fail:
    /* If this was no regular exit from the loop we have to continue
     * processing messages until we received PA_MESSAGE_SHUTDOWN */
    pa_asyncmsgq_post(u->thread_mq.outq, PA_MSGOBJECT(u->core), PA_CORE_MESSAGE_UNLOAD_MODULE, u->module, 0, NULL, NULL);
    pa_asyncmsgq_wait_for(u->thread_mq.inq, PA_MESSAGE_SHUTDOWN);

finish:
    pa_log_debug("Thread shutting down");
}

static int do_write(struct userdata *u) {
    ssize_t r;
    pa_assert(u);

    if (!pa_iochannel_is_writable(u->io))
        return 0;

    if (u->write_data) {
        pa_assert(u->write_index < u->write_length);

        if ((r = pa_iochannel_write(u->io, (uint8_t*) u->write_data + u->write_index, u->write_length - u->write_index)) <= 0) {
            pa_log("write() failed: %s", pa_cstrerror(errno));
            return -1;
        }

        u->write_index += r;
        pa_assert(u->write_index <= u->write_length);

        if (u->write_index == u->write_length) {
            pa_xfree(u->write_data);
            u->write_data = NULL;
            u->write_index = u->write_length = 0;
        }
    }

    if (!u->write_data && u->state == STATE_PREPARE) {
        /* OK, we're done with sending all control data we need to, so
         * let's hand the socket over to the IO thread now */

        pa_assert(u->fd < 0);
        u->fd = pa_iochannel_get_send_fd(u->io);

        pa_iochannel_set_noclose(u->io, TRUE);
        pa_iochannel_free(u->io);
        u->io = NULL;

        pa_make_tcp_socket_low_delay(u->fd);

        pa_log_debug("Connection authenticated, handing fd to IO thread...");

        pa_asyncmsgq_post(u->thread_mq.inq, PA_MSGOBJECT(u->sink), SINK_MESSAGE_PASS_SOCKET, NULL, 0, NULL, NULL);
        u->state = STATE_RUNNING;
    }

    return 0;
}

static int handle_response(struct userdata *u) {
    pa_assert(u);

    switch (u->state) {

        case STATE_AUTH:
            pa_assert(u->read_length == sizeof(int32_t));

            /* Process auth data */
            if (!*(int32_t*) u->read_data) {
                pa_log("Authentication failed: %s", pa_cstrerror(errno));
                return -1;
            }

            /* Request latency data */
            pa_assert(!u->write_data);
            *(int32_t*) (u->write_data = pa_xmalloc(u->write_length = sizeof(int32_t))) = ESD_PROTO_LATENCY;

            u->write_index = 0;
            u->state = STATE_LATENCY;

            /* Space for next response */
            pa_assert(u->read_length >= sizeof(int32_t));
            u->read_index = 0;
            u->read_length = sizeof(int32_t);

            break;

        case STATE_LATENCY: {
            int32_t *p;
            pa_assert(u->read_length == sizeof(int32_t));

            /* Process latency info */
            u->latency = (pa_usec_t) ((double) (*(int32_t*) u->read_data) * 1000000 / 44100);
            if (u->latency > 10000000) {
                pa_log_warn("Invalid latency information received from server");
                u->latency = 0;
            }

            /* Create stream */
            pa_assert(!u->write_data);
            p = u->write_data = pa_xmalloc0(u->write_length = sizeof(int32_t)*3+ESD_NAME_MAX);
            *(p++) = ESD_PROTO_STREAM_PLAY;
            *(p++) = u->format;
            *(p++) = u->rate;
            pa_strlcpy((char*) p, "PulseAudio Tunnel", ESD_NAME_MAX);

            u->write_index = 0;
            u->state = STATE_PREPARE;

            /* Don't read any further */
            pa_xfree(u->read_data);
            u->read_data = NULL;
            u->read_index = u->read_length = 0;

            break;
        }

        default:
            pa_assert_not_reached();
    }

    return 0;
}

static int do_read(struct userdata *u) {
    pa_assert(u);

    if (!pa_iochannel_is_readable(u->io))
        return 0;

    if (u->state == STATE_AUTH || u->state == STATE_LATENCY) {
        ssize_t r;

        if (!u->read_data)
            return 0;

        pa_assert(u->read_index < u->read_length);

        if ((r = pa_iochannel_read(u->io, (uint8_t*) u->read_data + u->read_index, u->read_length - u->read_index)) <= 0) {
            pa_log("read() failed: %s", r < 0 ? pa_cstrerror(errno) : "EOF");
            return -1;
        }

        u->read_index += r;
        pa_assert(u->read_index <= u->read_length);

        if (u->read_index == u->read_length)
            return handle_response(u);
    }

    return 0;
}

static void io_callback(PA_GCC_UNUSED pa_iochannel *io, void*userdata) {
    struct userdata *u = userdata;
    pa_assert(u);

    if (do_read(u) < 0 || do_write(u) < 0) {

        if (u->io) {
            pa_iochannel_free(u->io);
            u->io = NULL;
        }

       pa_module_unload_request(u->module);
    }
}

static void on_connection(PA_GCC_UNUSED pa_socket_client *c, pa_iochannel*io, void *userdata) {
    struct userdata *u = userdata;

    pa_socket_client_unref(u->client);
    u->client = NULL;

    if (!io) {
        pa_log("Connection failed: %s", pa_cstrerror(errno));
        pa_module_unload_request(u->module);
        return;
    }

    pa_assert(!u->io);
    u->io = io;
    pa_iochannel_set_callback(u->io, io_callback, u);

    pa_log_debug("Connection established, authenticating ...");
}

int pa__init(pa_module*m) {
    struct userdata *u = NULL;
    const char *p;
    pa_sample_spec ss;
    pa_modargs *ma = NULL;
    char *t;
    const char *espeaker;
    uint32_t key;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("failed to parse module arguments");
        goto fail;
    }

    ss = m->core->default_sample_spec;
    if (pa_modargs_get_sample_spec(ma, &ss) < 0) {
        pa_log("invalid sample format specification");
        goto fail;
    }

    if ((ss.format != PA_SAMPLE_U8 && ss.format != PA_SAMPLE_S16NE) ||
        (ss.channels > 2)) {
        pa_log("esound sample type support is limited to mono/stereo and U8 or S16NE sample data");
        goto fail;
    }

    u = pa_xnew0(struct userdata, 1);
    u->core = m->core;
    u->module = m;
    m->userdata = u;
    u->fd = -1;
    u->smoother = pa_smoother_new(PA_USEC_PER_SEC, PA_USEC_PER_SEC*2, TRUE);
    pa_memchunk_reset(&u->memchunk);
    u->offset = 0;

    pa_thread_mq_init(&u->thread_mq, m->core->mainloop);
    u->rtpoll = pa_rtpoll_new();
    pa_rtpoll_item_new_asyncmsgq(u->rtpoll, PA_RTPOLL_EARLY, u->thread_mq.inq);
    u->rtpoll_item = NULL;

    u->format =
        (ss.format == PA_SAMPLE_U8 ? ESD_BITS8 : ESD_BITS16) |
        (ss.channels == 2 ? ESD_STEREO : ESD_MONO);
    u->rate = ss.rate;
    u->block_size = pa_usec_to_bytes(PA_USEC_PER_SEC/20, &ss);

    u->read_data = u->write_data = NULL;
    u->read_index = u->write_index = u->read_length = u->write_length = 0;

    u->state = STATE_AUTH;
    u->latency = 0;

    if (!(u->sink = pa_sink_new(m->core, __FILE__, pa_modargs_get_value(ma, "sink_name", DEFAULT_SINK_NAME), 0, &ss, NULL))) {
        pa_log("Failed to create sink.");
        goto fail;
    }

    u->sink->parent.process_msg = sink_process_msg;
    u->sink->userdata = u;
    u->sink->flags = PA_SINK_LATENCY|PA_SINK_NETWORK;

    pa_sink_set_module(u->sink, m);
    pa_sink_set_asyncmsgq(u->sink, u->thread_mq.inq);
    pa_sink_set_rtpoll(u->sink, u->rtpoll);

    if (!(espeaker = getenv("ESPEAKER")))
        espeaker = ESD_UNIX_SOCKET_NAME;

    if (!(u->client = pa_socket_client_new_string(u->core->mainloop, p = pa_modargs_get_value(ma, "server", espeaker), ESD_DEFAULT_PORT))) {
        pa_log("Failed to connect to server.");
        goto fail;
    }

    pa_sink_set_description(u->sink, t = pa_sprintf_malloc("Esound sink '%s'", p));
    pa_xfree(t);

    pa_socket_client_set_callback(u->client, on_connection, u);

    /* Prepare the initial request */
    u->write_data = pa_xmalloc(u->write_length = ESD_KEY_LEN + sizeof(int32_t));
    if (pa_authkey_load_auto(pa_modargs_get_value(ma, "cookie", ".esd_auth"), u->write_data, ESD_KEY_LEN) < 0) {
        pa_log("Failed to load cookie");
        goto fail;
    }

    key = ESD_ENDIAN_KEY;
    memcpy((uint8_t*) u->write_data + ESD_KEY_LEN, &key, sizeof(key));

    /* Reserve space for the response */
    u->read_data = pa_xmalloc(u->read_length = sizeof(int32_t));

    if (!(u->thread = pa_thread_new(thread_func, u))) {
        pa_log("Failed to create thread.");
        goto fail;
    }

    pa_sink_put(u->sink);

    pa_modargs_free(ma);

    return 0;

fail:
    if (ma)
        pa_modargs_free(ma);

    pa__done(m);

    return -1;
}

void pa__done(pa_module*m) {
    struct userdata *u;
    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->sink)
        pa_sink_unlink(u->sink);

    if (u->thread) {
        pa_asyncmsgq_send(u->thread_mq.inq, NULL, PA_MESSAGE_SHUTDOWN, NULL, 0, NULL);
        pa_thread_free(u->thread);
    }

    pa_thread_mq_done(&u->thread_mq);

    if (u->sink)
        pa_sink_unref(u->sink);

    if (u->io)
        pa_iochannel_free(u->io);

    if (u->rtpoll_item)
        pa_rtpoll_item_free(u->rtpoll_item);

    if (u->rtpoll)
        pa_rtpoll_free(u->rtpoll);

    if (u->memchunk.memblock)
        pa_memblock_unref(u->memchunk.memblock);

    if (u->client)
        pa_socket_client_unref(u->client);

    pa_xfree(u->read_data);
    pa_xfree(u->write_data);

    if (u->smoother)
        pa_smoother_free(u->smoother);

    if (u->fd >= 0)
        pa_close(u->fd);

    pa_xfree(u);
}
