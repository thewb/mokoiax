#ifndef foostrlisthfoo
#define foostrlisthfoo

/* $Id: strlist.h 1984 2007-10-29 20:30:15Z lennart $ */

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

typedef struct pa_strlist pa_strlist;

/* Add the specified server string to the list, return the new linked list head */
pa_strlist* pa_strlist_prepend(pa_strlist *l, const char *s);

/* Remove the specified string from the list, return the new linked list head */
pa_strlist* pa_strlist_remove(pa_strlist *l, const char *s);

/* Make a whitespace separated string of all server stringes. Returned memory has to be freed with pa_xfree() */
char *pa_strlist_tostring(pa_strlist *l);

/* Free the entire list */
void pa_strlist_free(pa_strlist *l);

/* Return the next entry in the list in *string and remove it from
 * the list. Returns the new list head. The memory *string points to
 * has to be freed with pa_xfree() */
pa_strlist* pa_strlist_pop(pa_strlist *l, char **s);

/* Parse a whitespace separated server list */
pa_strlist* pa_strlist_parse(const char *s);

/* Reverse string list */
pa_strlist *pa_strlist_reverse(pa_strlist *l);

#endif
