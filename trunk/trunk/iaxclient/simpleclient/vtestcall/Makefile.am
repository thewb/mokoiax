bin_PROGRAMS=vtestcall
vtestcall_SOURCES=vtestcall.c

AM_CPPFLAGS=-I$(top_srcdir)/lib $(SDL_CFLAGS)
vtestcall_LDADD=$(top_builddir)/lib/libiaxclient.la $(SDL_LIBS)

EXTRA_DIST = vtestcall.vcproj
