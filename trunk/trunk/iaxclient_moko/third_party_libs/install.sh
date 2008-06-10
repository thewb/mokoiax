#!/bin/bash
. ../setup-env
echo "----Building and Installing Third Party Libraries----"
if [ ! -d /usr/local/openmoko/arm ] 
then
	echo "Please download and extract the OM toolchain. Look here: http://wiki.openmoko.org/wiki/Toolchain"
	exit 0
fi
echo "Skipping Alsa, as we are using PortAudio"
sleep 1
for i in alsa-lib-1.0.15 libogg-1.1.3 portaudio speex-1.2beta3 
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
		./configure --host=arm-angstrom-linux-gnueabi --prefix=/usr/local/openmoko/arm && make && make install
	fi
	echo -en "\n\nIf there is an error in compiling ${i}, Ctrl+c and fix it.\n\n"
	sleep 3
	cd ../
done
echo "----Done----"

