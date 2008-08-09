# Copyright (C) 2008 Brandon Kruse <bkruse@openmoko.org>
# Released under the MIT license (see COPYING.MIT for the terms)

DESCRIPTION = "A portable audio library"
HOMEPAGE = "http://portaudio.com"
LICENSE = "BSD"
SECTION = "libs"
PR = "r0"

SRC_URI = "http://portaudio.com/archives/pa_snapshot_${PV}.tar.gz"

# extracts to portaudio/

S = "${WORKDIR}/portaudio"

inherit autotools pkgconfig 

# I know this is a hack, but I am just getting it to work

do_configure() {
	./configure --host=arm-angstrom-linux-gnueabi --prefix=/usr/local/openmoko/arm 
}

do_install() {
	make
	make install
}
