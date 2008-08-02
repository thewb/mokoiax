# Copyright (C) 2008 Digium
# Brandon Kruse <bkruse@openmoko.org>
# Russell Bryant <russell@russellbryant.net>
# Released under the GPL v3 license

DESCRIPTION = "iaxclient library and more :D"
SECTION = "bkruse/mokoiax"

LICENSE = "GPLv3"

PN = "mokoiax"
PV = "0.1"

#for a tarball use:
SRC_URI = "http://mokoiax.googlecode.com/svn/tarballs/${PN}-${PV}.tar.gz"

#include standard rules to build a autoconf/automake package
inherit autotools

