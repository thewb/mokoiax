#!/bin/bash

for n in `svn status | cut -f7 -d' '` ; do
	DIR=`dirname $n`
	FILE=`basename $n`
	OLDVAL=`svn pg svn:ignore ${DIR}`
	printf "${OLDVAL}\n${FILE}" > footmp
	svn ps svn:ignore -F footmp ${DIR}
	rm -f footmp
done
