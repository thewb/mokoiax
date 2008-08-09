#ifndef foosdphfoo
#define foosdphfoo

/* $Id: sdp.h 1465 2007-05-29 17:24:48Z lennart $ */

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

#include <inttypes.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <pulse/sample.h>

#define PA_SDP_HEADER "v=0\n"

typedef struct pa_sdp_info {
    char *origin;
    char *session_name;

    struct sockaddr_storage sa;
    socklen_t salen;

    pa_sample_spec sample_spec;
    uint8_t payload;
} pa_sdp_info;

char *pa_sdp_build(int af, const void *src, const void *dst, const char *name, uint16_t port, uint8_t payload, const pa_sample_spec *ss);

pa_sdp_info *pa_sdp_parse(const char *t, pa_sdp_info *info, int is_goodbye);

void pa_sdp_info_destroy(pa_sdp_info *i);

#endif