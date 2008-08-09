# Copyright (C) 2008 Brandon Kruse <bkruse@openmoko.org> 
# Released under the MIT license (see COPYING.MIT for the terms)

DESCRIPTION = "Speex is an Open Source/Free Software patent-free audio compression format designed for speech."
SECTION = "libs"
LICENSE = "BSD"
HOMEPAGE = "http://www.speex.org"
DEPENDS = "libogg"
PR = "r3"

SRC_URI = "http://downloads.xiph.org/releases/speex/speex-${PV}.tar.gz"

S = "${WORKDIR}/speex-${PV}"

inherit autotools pkgconfig

do_configure_append() {
	sed -i s/"^OGG_CFLAGS.*$"/"OGG_CFLAGS = "/g Makefile */Makefile */*/Makefile
	sed -i s/"^OGG_LIBS.*$"/"OGG_LIBS = -logg"/g Makefile */Makefile */*/Makefile
	perl -pi -e 's:\s*-I/usr/include$::g' Makefile */Makefile */*/Makefile
}

do_configure() {
	./configure --host=arm-angstrom-linux-gnueabi --prefix=/usr/local/openmoko/arm 
}

do_install() {
	make install
}
do_stage() {
	oe_libinstall -C libspeex -so libspeex ${STAGING_LIBDIR}
	install -d ${STAGING_INCDIR}/speex
	install -m 0644 include/speex/*.h ${STAGING_INCDIR}/speex
	install -m 0644 speex.m4 ${STAGING_DATADIR}/aclocal/
}
