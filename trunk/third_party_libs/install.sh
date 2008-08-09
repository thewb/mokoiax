#!/bin/bash
. ../setup-env
echo "----Building and Installing Third Party Libraries----"
if [ ! -d /usr/local/openmoko/arm ] 
then
	echo "Please download and extract the OM toolchain. Look here: http://wiki.openmoko.org/wiki/Toolchain"
	exit 0
fi
sleep 1
for i in alsa-lib-1.0.15 libogg-1.1.3 speex-1.2beta3 libsndfile-1.0.17 libsamplerate-0.1.3 liboil-0.3.14 libatomic_ops-1.2 glib-2.16.3 
#for i in libogg-1.1.3 portaudio speex-1.2beta3 
do
	echo -en "\n\n\nBuilding the $i Library\n\n\n"
	sleep 3
	cd $i
	if [ "${i}" == "alsa-lib-1.0.15" ]
	then
		echo "Configuring alsa WITHOUT Python."
		./configure --host=arm-angstrom-linux-gnueabi --prefix=/usr/local/openmoko/arm --disable-python && make && make install
	else
		if [ ! -f "configure" ] 
		then
		    autoconf
		fi
		./configure --host=arm-angstrom-linux-gnueabi --prefix=/usr/local/openmoko/arm && make && make install
	fi
	echo -en "\n\nIf there is an error in compiling ${i}, Ctrl+c and fix it.\n\n"
	sleep 3
	cd ../
done
echo "----Done----"

