/* $Id: sap.c 1971 2007-10-28 19:13:50Z lennart $ */

/***
  This file is part of PulseAudio.

  Copyright 2006 Lennart Poettering

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

#include <time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif

#include <pulse/xmalloc.h>

#include <pulsecore/core-error.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>

#include "sap.h"
#include "sdp.h"

#define MIME_TYPE "application/sdp"

pa_sap_context* pa_sap_context_init_send(pa_sap_context *c, int fd, char *sdp_data) {
    pa_assert(c);
    pa_assert(fd >= 0);
    pa_assert(sdp_data);

    c->fd = fd;
    c->sdp_data = sdp_data;
    c->msg_id_hash = (uint16_t) (rand()*rand());

    return c;
}

void pa_sap_context_destroy(pa_sap_context *c) {
    pa_assert(c);

    pa_close(c->fd);
    pa_xfree(c->sdp_data);
}

int pa_sap_send(pa_sap_context *c, int goodbye) {
    uint32_t header;
    struct sockaddr_storage sa_buf;
    struct sockaddr *sa = (struct sockaddr*) &sa_buf;
    socklen_t salen = sizeof(sa_buf);
    struct iovec iov[4];
    struct msghdr m;
    int k;

    if (getsockname(c->fd, sa, &salen) < 0) {
        pa_log("getsockname() failed: %s\n", pa_cstrerror(errno));
        return -1;
    }

    pa_assert(sa->sa_family == AF_INET || sa->sa_family == AF_INET6);

    header = htonl(((uint32_t) 1 << 29) |
                   (sa->sa_family == AF_INET6 ? (uint32_t) 1 << 28 : 0) |
                   (goodbye ? (uint32_t) 1 << 26 : 0) |
                   (c->msg_id_hash));

    iov[0].iov_base = &header;
    iov[0].iov_len = sizeof(header);

    iov[1].iov_base = sa->sa_family == AF_INET ? (void*) &((struct sockaddr_in*) sa)->sin_addr : (void*) &((struct sockaddr_in6*) sa)->sin6_addr;
    iov[1].iov_len = sa->sa_family == AF_INET ? 4 : 16;

    iov[2].iov_base = (char*) MIME_TYPE;
    iov[2].iov_len = sizeof(MIME_TYPE);

    iov[3].iov_base = c->sdp_data;
    iov[3].iov_len = strlen(c->sdp_data);

    m.msg_name = NULL;
    m.msg_namelen = 0;
    m.msg_iov = iov;
    m.msg_iovlen = 4;
    m.msg_control = NULL;
    m.msg_controllen = 0;
    m.msg_flags = 0;

    if ((k = sendmsg(c->fd, &m, MSG_DONTWAIT)) < 0)
        pa_log_warn("sendmsg() failed: %s\n", pa_cstrerror(errno));

    return k;
}

pa_sap_context* pa_sap_context_init_recv(pa_sap_context *c, int fd) {
    pa_assert(c);
    pa_assert(fd >= 0);

    c->fd = fd;
    c->sdp_data = NULL;
    return c;
}

int pa_sap_recv(pa_sap_context *c, int *goodbye) {
    struct msghdr m;
    struct iovec iov;
    int size, k;
    char *buf = NULL, *e;
    uint32_t header;
    int six, ac;
    ssize_t r;

    pa_assert(c);
    pa_assert(goodbye);

    if (ioctl(c->fd, FIONREAD, &size) < 0) {
        pa_log_warn("FIONREAD failed: %s", pa_cstrerror(errno));
        goto fail;
    }

    buf = pa_xnew(char, size+1);
    buf[size] = 0;

    iov.iov_base = buf;
    iov.iov_len = size;

    m.msg_name = NULL;
    m.msg_namelen = 0;
    m.msg_iov = &iov;
    m.msg_iovlen = 1;
    m.msg_control = NULL;
    m.msg_controllen = 0;
    m.msg_flags = 0;

    if ((r = recvmsg(c->fd, &m, 0)) != size) {
        pa_log_warn("recvmsg() failed: %s", r < 0 ? pa_cstrerror(errno) : "size mismatch");
        goto fail;
    }

    if (size < 4) {
        pa_log_warn("SAP packet too short.");
        goto fail;
    }

    memcpy(&header, buf, sizeof(uint32_t));
    header = ntohl(header);

    if (header >> 29 != 1) {
        pa_log_warn("Unsupported SAP version.");
        goto fail;
    }

    if ((header >> 25) & 1) {
        pa_log_warn("Encrypted SAP not supported.");
        goto fail;
    }

    if ((header >> 24) & 1) {
        pa_log_warn("Compressed SAP not supported.");
        goto fail;
    }

    six = (header >> 28) & 1;
    ac = (header >> 16) & 0xFF;

    k = 4 + (six ? 16 : 4) + ac*4;
    if (size < k) {
        pa_log_warn("SAP packet too short (AD).");
        goto fail;
    }

    e = buf + k;
    size -= k;

    if ((unsigned) size >= sizeof(MIME_TYPE) && !strcmp(e, MIME_TYPE)) {
        e += sizeof(MIME_TYPE);
        size -= sizeof(MIME_TYPE);
    } else if ((unsigned) size < sizeof(PA_SDP_HEADER)-1 || strncmp(e, PA_SDP_HEADER, sizeof(PA_SDP_HEADER)-1)) {
        pa_log_warn("Invalid SDP header.");
        goto fail;
    }

    if (c->sdp_data)
        pa_xfree(c->sdp_data);

    c->sdp_data = pa_xstrndup(e, size);
    pa_xfree(buf);

    *goodbye = !!((header >> 26) & 1);

    return 0;

fail:
    pa_xfree(buf);

    return -1;
}
