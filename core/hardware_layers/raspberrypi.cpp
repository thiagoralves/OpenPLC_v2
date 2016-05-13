//-----------------------------------------------------------------------------
// Copyright 2015 Thiago Alves
//
// Based on the LDmicro software by Jonathan Westhues
// This file is part of the OpenPLC Software Stack.
//
// OpenPLC is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// OpenPLC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OpenPLC.  If not, see <http://www.gnu.org/licenses/>.
//------
//
// This file is the hardware layer for the OpenPLC. If you change the platform
// where it is running, you may only need to change this file. All the I/O
// related stuff is here. Basically it provides functions to read and write
// to the OpenPLC internal buffers in order to update I/O state.
// Thiago Alves, Dec 2015
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wiringPi.h>
#include <wiringSerial.h>
#include <pthread.h>

#include "ladder.h"

#define MAX_INPUT 		7
#define MAX_OUTPUT 		5

//inBufferPinMask: pin mask for each input, which
//means what pin is mapped to that OpenPLC input
int inBufferPinMask[MAX_INPUT] = { 7,  0,  2,  3, 12, 13, 14 };

//outBufferPinMask: pin mask for each output, which
//means what pin is mapped to that OpenPLC output
int outBufferPinMask[MAX_OUTPUT] =	{ 4,  5,  6, 10, 11 };

//-----------------------------------------------------------------------------
// This function is called by the main OpenPLC routine when it is initializing.
// Hardware initialization procedures should be here.
//-----------------------------------------------------------------------------
void initializeHardware()
{
	wiringPiSetup();
	//piHiPri(99);

	//set pins as input
	for (int i = 0; i < MAX_INPUT; i++)
	{
		pinMode(inBufferPinMask[i], INPUT);
		pullUpDnControl(inBufferPinMask[i], PUD_DOWN); //pull down enabled
	}

	//set pins as output
	for (int i = 0; i < MAX_OUTPUT; i++)
	{
		pinMode(outBufferPinMask[i], OUTPUT);
	}
}

//-----------------------------------------------------------------------------
// This function is called by the OpenPLC in a loop. Here the internal buffers
// must be updated to reflect the actual I/O state. The mutex buffer_lock
// must be used to protect access to the buffers on a threaded environment.
//-----------------------------------------------------------------------------
void updateBuffers()
{
	pthread_mutex_lock(&bufferLock); //lock mutex

	//INPUT
	for (int i = 0; i < MAX_INPUT; i++)
	{
		if (bool_input[0][i] != NULL) *bool_input[0][i] = digitalRead(inBufferPinMask[i]);
		/*
		cout << "X1_" << i << ": ";
		if (inBuffer[i])
			cout << "1" << endl;
		else
			cout << "0" << endl;
		*/
	}

	//OUTPUT
	for (int i = 0; i < MAX_OUTPUT; i++)
	{
		if (bool_output[0][i] != NULL) digitalWrite(outBufferPinMask[i], *bool_output[0][i]);
		/*
		cout << "Y1_" << i << ": ";
		if (outBuffer[i])
			cout << "1" << endl;
		else
			cout << "0" << endl;
		*/
	}

	pthread_mutex_unlock(&bufferLock); //unlock mutex
}

