/* $Id: pax11publish.c 1426 2007-02-13 15:35:19Z ossman $ */

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

#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <assert.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <pulse/util.h>

#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/authkey.h>
#include <pulsecore/native-common.h>
#include <pulsecore/x11prop.h>

#include "../pulse/client-conf.h"

int main(int argc, char *argv[]) {
    const char *dname = NULL, *sink = NULL, *source = NULL, *server = NULL, *cookie_file = PA_NATIVE_COOKIE_FILE;
    int c, ret = 1;
    Display *d = NULL;
    enum { DUMP, EXPORT, IMPORT, REMOVE } mode = DUMP;

    while ((c = getopt(argc, argv, "deiD:S:O:I:c:hr")) != -1) {
        switch (c) {
            case 'D' :
                dname = optarg;
                break;
            case 'h':
                printf("%s [-D display] [-S server] [-O sink] [-I source] [-c file]  [-d|-e|-i|-r]\n\n"
                       " -d    Show current PulseAudio data attached to X11 display (default)\n"
                       " -e    Export local PulseAudio data to X11 display\n"
                       " -i    Import PulseAudio data from X11 display to local environment variables and cookie file.\n"
                       " -r    Remove PulseAudio data from X11 display\n",
                       pa_path_get_filename(argv[0]));
                ret = 0;
                goto finish;
            case 'd':
                mode = DUMP;
                break;
            case 'e':
                mode = EXPORT;
                break;
            case 'i':
                mode = IMPORT;
                break;
            case 'r':
                mode = REMOVE;
                break;
            case 'c':
                cookie_file = optarg;
                break;
            case 'I':
                source = optarg;
                break;
            case 'O':
                sink = optarg;
                break;
            case 'S':
                server = optarg;
                break;
            default:
                fprintf(stderr, "Failed to parse command line.\n");
                goto finish;
        }
    }

    if (!(d = XOpenDisplay(dname))) {
        pa_log("XOpenDisplay() failed");
        goto finish;
    }

    switch (mode) {
        case DUMP: {
            char t[1024];
            if (pa_x11_get_prop(d, "PULSE_SERVER", t, sizeof(t)))
                printf("Server: %s\n", t);
            if (pa_x11_get_prop(d, "PULSE_SOURCE", t, sizeof(t)))
                printf("Source: %s\n", t);
            if (pa_x11_get_prop(d, "PULSE_SINK", t, sizeof(t)))
                printf("Sink: %s\n", t);
            if (pa_x11_get_prop(d, "PULSE_COOKIE", t, sizeof(t)))
                printf("Cookie: %s\n", t);

            break;
        }

        case IMPORT: {
            char t[1024];
            if (pa_x11_get_prop(d, "PULSE_SERVER", t, sizeof(t)))
                printf("PULSE_SERVER='%s'\nexport PULSE_SERVER\n", t);
            if (pa_x11_get_prop(d, "PULSE_SOURCE", t, sizeof(t)))
                printf("PULSE_SOURCE='%s'\nexport PULSE_SOURCE\n", t);
            if (pa_x11_get_prop(d, "PULSE_SINK", t, sizeof(t)))
                printf("PULSE_SINK='%s'\nexport PULSE_SINK\n", t);

            if (pa_x11_get_prop(d, "PULSE_COOKIE", t, sizeof(t))) {
                uint8_t cookie[PA_NATIVE_COOKIE_LENGTH];
                size_t l;
                if ((l = pa_parsehex(t, cookie, sizeof(cookie))) != sizeof(cookie)) {
                    fprintf(stderr, "Failed to parse cookie data\n");
                    goto finish;
                }

                if (pa_authkey_save(cookie_file, cookie, l) < 0) {
                    fprintf(stderr, "Failed to save cookie data\n");
                    goto finish;
                }
            }

            break;
        }

        case EXPORT: {
            pa_client_conf *conf = pa_client_conf_new();
            uint8_t cookie[PA_NATIVE_COOKIE_LENGTH];
            char hx[PA_NATIVE_COOKIE_LENGTH*2+1];
            assert(conf);

            if (pa_client_conf_load(conf, NULL) < 0) {
                fprintf(stderr, "Failed to load client configuration file.\n");
                goto finish;
            }

            if (pa_client_conf_env(conf) < 0) {
                fprintf(stderr, "Failed to read environment configuration data.\n");
                goto finish;
            }

            pa_x11_del_prop(d, "PULSE_SERVER");
            pa_x11_del_prop(d, "PULSE_SINK");
            pa_x11_del_prop(d, "PULSE_SOURCE");
            pa_x11_del_prop(d, "PULSE_ID");
            pa_x11_del_prop(d, "PULSE_COOKIE");

            if (server)
                pa_x11_set_prop(d, "PULSE_SERVER", server);
            else if (conf->default_server)
                pa_x11_set_prop(d, "PULSE_SERVER", conf->default_server);
            else {
                char hn[256];
                if (!pa_get_fqdn(hn, sizeof(hn))) {
                    fprintf(stderr, "Failed to get FQDN.\n");
                    goto finish;
                }

                pa_x11_set_prop(d, "PULSE_SERVER", hn);
            }

            if (sink)
                pa_x11_set_prop(d, "PULSE_SINK", sink);
            else if (conf->default_sink)
                pa_x11_set_prop(d, "PULSE_SINK", conf->default_sink);

            if (source)
                pa_x11_set_prop(d, "PULSE_SOURCE", source);
            if (conf->default_source)
                pa_x11_set_prop(d, "PULSE_SOURCE", conf->default_source);

            pa_client_conf_free(conf);

            if (pa_authkey_load_auto(cookie_file, cookie, sizeof(cookie)) < 0) {
                fprintf(stderr, "Failed to load cookie data\n");
                goto finish;
            }

            pa_x11_set_prop(d, "PULSE_COOKIE", pa_hexstr(cookie, sizeof(cookie), hx, sizeof(hx)));
            break;
        }

        case REMOVE:
            pa_x11_del_prop(d, "PULSE_SERVER");
            pa_x11_del_prop(d, "PULSE_SINK");
            pa_x11_del_prop(d, "PULSE_SOURCE");
            pa_x11_del_prop(d, "PULSE_ID");
            pa_x11_del_prop(d, "PULSE_COOKIE");
            break;

        default:
            fprintf(stderr, "No yet implemented.\n");
            goto finish;
    }

    ret = 0;

finish:

    if (d) {
        XSync(d, False);
        XCloseDisplay(d);
    }

    return ret;
}
