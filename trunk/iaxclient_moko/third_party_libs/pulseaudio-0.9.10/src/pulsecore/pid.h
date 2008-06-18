#ifndef foopidhfoo
#define foopidhfoo

/* $Id: pid.h 2067 2007-11-21 01:30:40Z lennart $ */

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

int pa_pid_file_create(void);
int pa_pid_file_remove(void);
int pa_pid_file_check_running(pid_t *pid, const char *binary_name);
int pa_pid_file_kill(int sig, pid_t *pid, const char *binary_name);

#endif