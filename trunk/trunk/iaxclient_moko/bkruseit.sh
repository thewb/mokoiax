#!/bin/bash

. setup-env
if [ "$1" == "clean" ]
then
	echo -en "\n\n----Cleaning Moko IAX----\n\n"
	a="third_party_libs"
	for i in ${a}/libogg-1.1.3 ${a}/portaudio ${a}/speex-1.2beta3 ${a}/alsa-lib-1.0.15 
	do
		cd ${i}
		make clean
		make distclean
		cd ../../
	done
	make clean
	make distclean
	echo -en "\n\n\n\n----Done----\n\n"
	exit 0
fi
		
if [ ! -d /usr/local/openmoko/arm ] 
then
	echo "Please download and extract the OM toolchain. Look here: http://wiki.openmoko.org/wiki/Toolchain"
	exit 0
fi

echo -en "\n\n----Building and Installing Moko IAX----\n\n"
sleep 3

cd lib/gsm
./configure 2> /dev/null
make
make install
cd ../../
cd third_party_libs
./install.sh
cd ../
./configure --host=arm-angstrom-linux-gnueabi --prefix=/usr/local/openmoko/arm  --without-video --without-theora --without-vidcap --enable-clients=testcall && make && make install
#./configure --host=arm-angstrom-linux-gnueabi --prefix=/usr/local/openmoko/arm  --without-video --without-theora --without-vidcap --disable-speex-preproces make && make install

echo -en "\n\n-----MokoiAX DONE Building!-----\n\n"
