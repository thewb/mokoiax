# Copyright (C) 2008 Digium
# Brandon Kruse <bkruse@openmoko.org>
# Russell Bryant <russell@russellbryant.net>
# Released under the GPL v3 license

DESCRIPTION = "iaxclient library and more :D"
SECTION = "bkruse/mokoiax"

LICENSE = "GPLv3"

PN = "mokoiax"
PV = "1.0"

SRCDATE_${PN} = "now"
SRC_URI = "svn://mokoiax.googlecode.com/svn/trunk/trunk/iaxclient_moko;proto=http;module=${PN}-${PV}"

#for a tarball use:
#SRC_URI = "http://server/path/package/${PN}-${PV}.tar.gz"

#include standard rules to build a autoconf/automake package
inherit autotools

