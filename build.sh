#!/bin/bash

set -e

echo Building OpenPLC environment:

echo [MATIEC COMPILER]
cd matiec_src
autoreconf -i
./configure
make

echo [LADDER]
cd ..
cp ./matiec_src/iec2c ./
./iec2c ./st_files/blank_program.st
mv -f POUS.c POUS.h LOCATED_VARIABLES.h VARIABLES.csv Config0.c Config0.h Res0.c ./core/

echo [ST OPTIMIZER]
cd st_optimizer_src
g++ st_optimizer.cpp -o st_optimizer
cd ..
cp ./st_optimizer_src/st_optimizer ./

echo [GLUE GENERATOR]
cd glue_generator_src
g++ glue_generator.cpp -o glue_generator
cd ..
cp ./glue_generator_src/glue_generator ./core/glue_generator

if [ -z "$DNP3_SUPPORT" ]; then
	clear
	echo OpenPLC can talk Modbus/TCP and DNP3 SCADA protocols. Modbus/TCP is already
	echo added to the system. Do you want to add support for DNP3 as well \(Y/N\)?
	read DNP3_SUPPORT
fi
if [ "$DNP3_SUPPORT" = "Y" -o "$DNP3_SUPPORT" = "y" -o "$DNP3_SUPPORT" = "yes" ]; then
	echo Installing DNP3 on the system...

	#moving files to the right place
	mv ./core/dnp3.disabled ./core/dnp3.cpp 2> /dev/null
	mv ./core/dnp3_dummy.cpp ./core/dnp3_dummy.disabled 2> /dev/null
	cp -f ./core/core_builders/dnp3_enabled/*.* ./core/core_builders/

	#make sure cmake is installed
	sudo apt-get install cmake

	#download opendnp3
	#git clone --recursive https://github.com/automatak/dnp3.git
	cd dnp3

	#create swapfile to prevent out of memory errors
	echo creating swapfile...
	sudo dd if=/dev/zero of=swapfile bs=1M count=1000
	sudo mkswap swapfile
	sudo swapon swapfile

	#build opendnp3
	cmake ../dnp3
	make
	sudo make install
	sudo ldconfig

	#remove swapfile
	sudo swapoff swapfile
	sudo rm -f ./swapfile
	cd ..
else
	echo Skipping DNP3 installation
	mv ./core/dnp3.cpp ./core/dnp3.disabled 2> /dev/null
	mv ./core/dnp3_dummy.disabled ./core/dnp3_dummy.cpp 2> /dev/null
	cp -f ./core/core_builders/dnp3_disabled/*.* ./core/core_builders/
fi
cd core
rm -f ./hardware_layer.cpp
rm -f ../build_core.sh
if [ -z "$COMM_DRIVER" ]; then
	echo The OpenPLC needs a driver to be able to control physical or virtual hardware.
	echo Please select the driver you would like to use:
	OPTIONS="Blank Modbus Fischertechnik RaspberryPi UniPi PiXtend PiXtend_2S Arduino ESP8266 Arduino+RaspberryPi Simulink "
	select opt in $OPTIONS; do
		COMM_DRIVER="$opt"
		break
	done
fi
case "$COMM_DRIVER" in
Blank)
	cp ./hardware_layers/blank.cpp ./hardware_layer.cpp
	cp ./core_builders/build_normal.sh ../build_core.sh
	cd ..
	;;
Modbus)
	cp ./hardware_layers/modbus_master.cpp ./hardware_layer.cpp
	cp ./core_builders/build_modbus.sh ../build_core.sh
	echo [LIBMODBUS]
	cd ..
	cd libmodbus_src
	./autogen.sh
	./configure --enable-static=yes
	make
	cd ..
	;;
Fischertechnik)
	cp ./hardware_layers/fischertechnik.cpp ./hardware_layer.cpp
	cp ./core_builders/build_rpi.sh ../build_core.sh
	cd ..
	;;
RaspberryPi)
	cp ./hardware_layers/raspberrypi.cpp ./hardware_layer.cpp
	cp ./core_builders/build_rpi.sh ../build_core.sh
	cd ..
	;;
UniPi)
	cp ./hardware_layers/unipi.cpp ./hardware_layer.cpp
	cp ./core_builders/build_rpi.sh ../build_core.sh
	cd ..
	;;
PiXtend)
	cp ./hardware_layers/pixtend.cpp ./hardware_layer.cpp
	cp ./core_builders/build_rpi.sh ../build_core.sh
	cd ..
	;;
PiXtend_2S)
	cp ./hardware_layers/pixtend2s.cpp ./hardware_layer.cpp
	cp ./core_builders/build_rpi.sh ../build_core.sh
	cd ..
	;;
Arduino)
	cp ./hardware_layers/arduino.cpp ./hardware_layer.cpp
	cp ./core_builders/build_normal.sh ../build_core.sh
	cd ..
	;;
ESP8266)
	cp ./hardware_layers/esp8266.cpp ./hardware_layer.cpp
	cp ./core_builders/build_normal.sh ../build_core.sh
	cd ..
	;;
Arduino+RaspberryPi)
	cp ./hardware_layers/arduino.cpp ./hardware_layer.cpp
	cp ./core_builders/build_rpi.sh ../build_core.sh
	cd ..
	;;
Simulink)
	cp ./hardware_layers/simulink.cpp ./hardware_layer.cpp
	cp ./core_builders/build_normal.sh ../build_core.sh
	cd ..
	;;
*)
	echo "Invalid driver option" 1>&2
	exit 1
esac

echo [OPENPLC]
./build_core.sh

