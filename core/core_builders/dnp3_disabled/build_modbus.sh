#!/bin/bash

set -e

cd core
echo Generating object files...
g++ -I ./lib -c Config0.c
g++ -I ./lib -c Res0.c

echo Generating glueVars.cpp
./glue_generator

# Include path
CXXFLAGS=(-I ./lib -I ../libmodbus_src/src)
# Compiler flags
CXXFLAGS+=(-pthread -fpermissive)
echo Compiling main program
g++ "${CXXFLAGS[@]}" *.cpp *.o -o openplc ../libmodbus_src/src/.libs/libmodbus.a
cd ..

