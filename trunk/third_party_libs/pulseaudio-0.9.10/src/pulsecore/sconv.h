#ifndef foosconvhfoo
#define foosconvhfoo

/* $Id: sconv.h 1971 2007-10-28 19:13:50Z lennart $ */

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

#include <pulse/sample.h>

typedef void (*pa_convert_func_t)(unsigned n, const void *a, void *b);

pa_convert_func_t pa_get_convert_to_float32ne_function(pa_sample_format_t f) PA_GCC_PURE;
pa_convert_func_t pa_get_convert_from_float32ne_function(pa_sample_format_t f) PA_GCC_PURE;

pa_convert_func_t pa_get_convert_to_s16ne_function(pa_sample_format_t f) PA_GCC_PURE;
pa_convert_func_t pa_get_convert_from_s16ne_function(pa_sample_format_t f) PA_GCC_PURE;

#endif
