# Copyright (C) 2008 Digium
# Brandon Kruse <bkruse@openmoko.org>
# Russell Bryant <russell@russellbryant.net>
# Released under the GPL v3 license

DESCRIPTION = "iaxclient library and more :D"
SECTION = "bkruse/mokoiax"

AUTHOR = "Brandon Kruse"
# DEPENDS = "speex portaudio"
# DEPENDS = "speex alsa-lib"
DEPENDS = "speex portaudio"
# RDEPENDS += "portaudio-dev speex"

# PROVIDES += "binary/testcall"

LICENSE = "GPLv3"

PN = "mokoiax"
PV = "0.1"

#for a tarball use:
SRC_URI = "http://mokoiax.googlecode.com/svn/trunk/trunk/tarballs/${PN}_${PV}.tar.gz"
# SRC_URI = "file:///usr/src/mokoiax-google/trunk/tarballs/${PN}_${PV}.tar.gz"

S = "${WORKDIR}/${PN}_${PV}"

#include standard rules to build a autoconf/automake package
inherit autotools

do_configure() {
	./configure --host=arm-angstrom-linux-gnueabi --prefix=/usr/local/openmoko/arm  --without-video --without-theora --without-vidcap --enable-video=no --enable-clients=testcall
}

do_install() {
	make
	make install
}

#do_stage() {
#	oe_libinstall -C libiaxclient -so libiaxclient ${STAGING_LIBDIR}
#}
