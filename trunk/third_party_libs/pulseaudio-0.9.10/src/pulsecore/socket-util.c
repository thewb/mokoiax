/* $Id: socket-util.c 1971 2007-10-28 19:13:50Z lennart $ */

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2004 Joe Marcus Clarke
  Copyright 2006-2007 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif
#ifdef HAVE_NETINET_IP_H
#include <netinet/ip.h>
#endif
#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifndef HAVE_INET_NTOP
#include "inet_ntop.h"
#endif

#include "winsock.h"

#include <pulse/xmalloc.h>

#include <pulsecore/core-error.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>

#include "socket-util.h"

void pa_socket_peer_to_string(int fd, char *c, size_t l) {
    struct stat st;

    pa_assert(fd >= 0);
    pa_assert(c);
    pa_assert(l > 0);

#ifndef OS_IS_WIN32
    pa_assert_se(fstat(fd, &st) == 0);
#endif

#ifndef OS_IS_WIN32
    if (S_ISSOCK(st.st_mode)) {
#endif
        union {
            struct sockaddr sa;
            struct sockaddr_in in;
            struct sockaddr_in6 in6;
#ifdef HAVE_SYS_UN_H
            struct sockaddr_un un;
#endif
        } sa;
        socklen_t sa_len = sizeof(sa);

        if (getpeername(fd, &sa.sa, &sa_len) >= 0) {

            if (sa.sa.sa_family == AF_INET) {
                uint32_t ip = ntohl(sa.in.sin_addr.s_addr);

                pa_snprintf(c, l, "TCP/IP client from %i.%i.%i.%i:%u",
                            ip >> 24,
                            (ip >> 16) & 0xFF,
                            (ip >> 8) & 0xFF,
                            ip & 0xFF,
                            ntohs(sa.in.sin_port));
                return;
            } else if (sa.sa.sa_family == AF_INET6) {
                char buf[INET6_ADDRSTRLEN];
                const char *res;

                res = inet_ntop(AF_INET6, &sa.in6.sin6_addr, buf, sizeof(buf));
                if (res) {
                    pa_snprintf(c, l, "TCP/IP client from [%s]:%u", buf, ntohs(sa.in6.sin6_port));
                    return;
                }
#ifdef HAVE_SYS_UN_H
            } else if (sa.sa.sa_family == AF_UNIX) {
                pa_snprintf(c, l, "UNIX socket client");
                return;
#endif
            }

        }
#ifndef OS_IS_WIN32
        pa_snprintf(c, l, "Unknown network client");
        return;
    } else if (S_ISCHR(st.st_mode) && (fd == 0 || fd == 1)) {
        pa_snprintf(c, l, "STDIN/STDOUT client");
        return;
    }
#endif /* OS_IS_WIN32 */

    pa_snprintf(c, l, "Unknown client");
}

void pa_make_socket_low_delay(int fd) {

#ifdef SO_PRIORITY
    int priority;
    pa_assert(fd >= 0);

    priority = 6;
    if (setsockopt(fd, SOL_SOCKET, SO_PRIORITY, (void*)&priority, sizeof(priority)) < 0)
        pa_log_warn("SO_PRIORITY failed: %s", pa_cstrerror(errno));
#endif
}

void pa_make_tcp_socket_low_delay(int fd) {
    pa_assert(fd >= 0);

    pa_make_socket_low_delay(fd);

#if defined(SOL_TCP) || defined(IPPROTO_TCP)
    {
        int on = 1;
#if defined(SOL_TCP)
        if (setsockopt(fd, SOL_TCP, TCP_NODELAY, (void*)&on, sizeof(on)) < 0)
#else
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void*)&on, sizeof(on)) < 0)
#endif
            pa_log_warn("TCP_NODELAY failed: %s", pa_cstrerror(errno));
    }
#endif

#if defined(IPTOS_LOWDELAY) && defined(IP_TOS) && (defined(SOL_IP) || defined(IPPROTO_IP))
    {
        int tos = IPTOS_LOWDELAY;
#ifdef SOL_IP
        if (setsockopt(fd, SOL_IP, IP_TOS, (void*)&tos, sizeof(tos)) < 0)
#else
        if (setsockopt(fd, IPPROTO_IP, IP_TOS, (void*)&tos, sizeof(tos)) < 0)
#endif
            pa_log_warn("IP_TOS failed: %s", pa_cstrerror(errno));
    }
#endif
}

void pa_make_udp_socket_low_delay(int fd) {
    pa_assert(fd >= 0);

    pa_make_socket_low_delay(fd);

#if defined(IPTOS_LOWDELAY) && defined(IP_TOS) && (defined(SOL_IP) || defined(IPPROTO_IP))
    {
        int tos = IPTOS_LOWDELAY;
#ifdef SOL_IP
        if (setsockopt(fd, SOL_IP, IP_TOS, (void*)&tos, sizeof(tos)) < 0)
#else
        if (setsockopt(fd, IPPROTO_IP, IP_TOS, (void*)&tos, sizeof(tos)) < 0)
#endif
            pa_log_warn("IP_TOS failed: %s", pa_cstrerror(errno));
    }
#endif
}

int pa_socket_set_rcvbuf(int fd, size_t l) {
    pa_assert(fd >= 0);

    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void*)&l, sizeof(l)) < 0) {
        pa_log_warn("SO_RCVBUF: %s", pa_cstrerror(errno));
        return -1;
    }

    return 0;
}

int pa_socket_set_sndbuf(int fd, size_t l) {
    pa_assert(fd >= 0);

    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void*)&l, sizeof(l)) < 0) {
        pa_log("SO_SNDBUF: %s", pa_cstrerror(errno));
        return -1;
    }

    return 0;
}

#ifdef HAVE_SYS_UN_H

int pa_unix_socket_is_stale(const char *fn) {
    struct sockaddr_un sa;
    int fd = -1, ret = -1;

    pa_assert(fn);

    if ((fd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
        pa_log("socket(): %s", pa_cstrerror(errno));
        goto finish;
    }

    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, fn, sizeof(sa.sun_path)-1);
    sa.sun_path[sizeof(sa.sun_path) - 1] = 0;

    if (connect(fd, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
        if (errno == ECONNREFUSED)
            ret = 1;
    } else
        ret = 0;

finish:
    if (fd >= 0)
        pa_close(fd);

    return ret;
}

int pa_unix_socket_remove_stale(const char *fn) {
    int r;

    pa_assert(fn);

    if ((r = pa_unix_socket_is_stale(fn)) < 0)
        return errno != ENOENT ? -1 : 0;

    if (!r)
        return 0;

    /* Yes, here is a race condition. But who cares? */
    if (unlink(fn) < 0)
        return -1;

    return 0;
}

#else /* HAVE_SYS_UN_H */

int pa_unix_socket_is_stale(const char *fn) {
    return -1;
}

int pa_unix_socket_remove_stale(const char *fn) {
    return -1;
}

#endif /* HAVE_SYS_UN_H */
