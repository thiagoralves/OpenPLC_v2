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
// This file has all the MODBUS/TCP functions supported by the OpenPLC. If any
// other function is to be added to the project, it must be added here
// Thiago Alves, Dec 2015
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "ladder.h"

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) (bitvalue ? bitSet(value, bit) : bitClear(value, bit))

#define lowByte(w) ((unsigned char) ((w) & 0xff))
#define highByte(w) ((unsigned char) ((w) >> 8))

//-----------------------------------------------------------------------------
// Class constructor. It receives a string with the request from the client
// and copies it to the internal buffer
//-----------------------------------------------------------------------------
Modbus::Modbus(unsigned char *request, int size)
{
	for (int i = 0; i < size; i++)
	{
		ByteArray[i] = request[i];
	}
}

//-----------------------------------------------------------------------------
// Set the appropriate MODBUS Function Code
//-----------------------------------------------------------------------------
void Modbus::SetFC(int fc)
{
	if(fc == 1) FC = MB_FC_READ_COILS;
	else if(fc == 2) FC = MB_FC_READ_INPUTS;
	else if(fc == 3) FC = MB_FC_READ_HOLDING_REGISTERS;
	else if(fc == 4) FC = MB_FC_READ_INPUT_REGISTERS;
	else if(fc == 5) FC = MB_FC_WRITE_COIL;
	else if(fc == 6) FC = MB_FC_WRITE_REGISTER;
	else if(fc == 15) FC = MB_FC_WRITE_MULTIPLE_COILS;
	else if(fc == 16) FC = MB_FC_WRITE_MULTIPLE_REGISTERS;

	else
	{
		FC = MB_FC_ERROR;
		ER = ERR_ILLEGAL_FUNCTION;
	}
}

//-----------------------------------------------------------------------------
// Concatenate two bytes into an int
//-----------------------------------------------------------------------------
int Modbus::word(unsigned char byte1, unsigned char byte2)
{
	int returnValue;
	returnValue = (int)(byte1 << 8) | (int)byte2;

	return returnValue;
}

//==================================================================
//              MODBUS FUNCTION CODES IMPLEMENTATION
//==================================================================
void Modbus::ReadCoils()
{
	int Start, ByteDataLength, CoilDataLength;

	Start = word(ByteArray[8],ByteArray[9]);
	CoilDataLength = word(ByteArray[10],ByteArray[11]);
	ByteDataLength = CoilDataLength / 8; //calculating the size of the message in bytes
	if(ByteDataLength * 8 < CoilDataLength) ByteDataLength++;
	CoilDataLength = ByteDataLength * 8;
	ByteArray[5] = ByteDataLength + 3; //Number of bytes after this one.
	ByteArray[8] = ByteDataLength;     //Number of bytes after this one (or number of bytes of data).

	pthread_mutex_lock(&bufferLock);
	for(int i = 0; i < ByteDataLength ; i++)
	{
		for(int j = 0; j < 8; j++)
		{
			int position = Start + i * 8 + j;
			if (position < (BUFFER_SIZE*BUFFER_SIZE))
			{
				if (bool_output[position/8][position%8] != NULL)
				{
					bitWrite(ByteArray[9 + i], j, *bool_output[position/8][position%8]);
				}
				else
				{
					bitWrite(ByteArray[9 + i], j, 0);
				}
			}
			else //invalid address
			{
				FC = MB_FC_ERROR;
				ER = ERR_ILLEGAL_DATA_ADDRESS;
			}
		}
	}
	pthread_mutex_unlock(&bufferLock);

	if (FC == MB_FC_ERROR)
	{
		ModbusError();
	}
	else
	{
		MessageLength = ByteDataLength + 9;
		Writes = 1 + Writes * (Writes < 999);
		FC = MB_FC_NONE;
	}
}

void Modbus::ReadDiscreteInputs()
{
	int Start, ByteDataLength, InputDataLength;

	Start = word(ByteArray[8],ByteArray[9]);
	InputDataLength = word(ByteArray[10],ByteArray[11]);
	ByteDataLength = InputDataLength / 8;
	if(ByteDataLength * 8 < InputDataLength) ByteDataLength++;
	InputDataLength = ByteDataLength * 8;
	ByteArray[5] = ByteDataLength + 3; //Number of bytes after this one.
	ByteArray[8] = ByteDataLength;     //Number of bytes after this one (or number of bytes of data).

	pthread_mutex_lock(&bufferLock);
	for(int i = 0; i < ByteDataLength ; i++)
	{
		for(int j = 0; j < 8; j++)
		{
			int position = Start + i * 8 + j;
			if (position < (BUFFER_SIZE*BUFFER_SIZE))
			{
				if (bool_input[position/8][position%8] != NULL)
				{
					bitWrite(ByteArray[9 + i], j, *bool_input[position/8][position%8]);
				}
				else
				{
					bitWrite(ByteArray[9 + i], j, 0);
				}
			}
			else //invalid address
			{
				FC = MB_FC_ERROR;
				ER = ERR_ILLEGAL_DATA_ADDRESS;
			}
		}
	}
	pthread_mutex_unlock(&bufferLock);

	if (FC == MB_FC_ERROR)
	{
		ModbusError();
	}
	else
	{
		MessageLength = ByteDataLength + 9;
		Writes = 1 + Writes * (Writes < 999);
		FC = MB_FC_NONE;
	}
}

void Modbus::ReadHoldingRegisters()
{
	int Start, WordDataLength, ByteDataLength;

	Start = word(ByteArray[8],ByteArray[9]);
	WordDataLength = word(ByteArray[10],ByteArray[11]);
	ByteDataLength = WordDataLength * 2;
	ByteArray[5] = ByteDataLength + 3; //Number of bytes after this one.
	ByteArray[8] = ByteDataLength;     //Number of bytes after this one (or number of bytes of data).

	pthread_mutex_lock(&bufferLock);
	for(int i = 0; i < WordDataLength; i++)
	{
		int position = Start + i;
		if (position < BUFFER_SIZE)
		{
			if (int_output[0][position] != NULL)
			{
				ByteArray[ 9 + i * 2] = highByte(*int_output[0][position]);
				ByteArray[10 + i * 2] =  lowByte(*int_output[0][position]);
			}
			else
			{
				ByteArray[ 9 + i * 2] = highByte(0);
				ByteArray[10 + i * 2] =  lowByte(0);
			}
		}
		else //invalid address
		{
			FC = MB_FC_ERROR;
			ER = ERR_ILLEGAL_DATA_ADDRESS;
		}
	}
	pthread_mutex_unlock(&bufferLock);

	if (FC == MB_FC_ERROR)
	{
		ModbusError();
	}
	else
	{
		MessageLength = ByteDataLength + 9;
		Writes = 1 + Writes * (Writes < 999);
		FC = MB_FC_NONE;
	}
}

void Modbus::ReadInputRegisters()
{
	int Start, WordDataLength, ByteDataLength;

	Start = word(ByteArray[8],ByteArray[9]);
	WordDataLength = word(ByteArray[10],ByteArray[11]);
	ByteDataLength = WordDataLength * 2;
	ByteArray[5] = ByteDataLength + 3; //Number of bytes after this one.
	ByteArray[8] = ByteDataLength;     //Number of bytes after this one (or number of bytes of data).

	pthread_mutex_lock(&bufferLock);
	for(int i = 0; i < WordDataLength; i++)
	{
		int position = Start + i;
		if (position < BUFFER_SIZE)
		{
			if (int_input[0][position] != NULL)
			{
				ByteArray[ 9 + i * 2] = highByte(*int_input[0][position]);
				ByteArray[10 + i * 2] =  lowByte(*int_input[0][position]);
			}
			else
			{
				ByteArray[ 9 + i * 2] = highByte(0);
				ByteArray[10 + i * 2] =  lowByte(0);
			}
		}
		else //invalid address
		{
			FC = MB_FC_ERROR;
			ER = ERR_ILLEGAL_DATA_ADDRESS;
		}
	}
	pthread_mutex_unlock(&bufferLock);

	if (FC == MB_FC_ERROR)
	{
		ModbusError();
	}
	else
	{
		MessageLength = ByteDataLength + 9;
		Writes = 1 + Writes * (Writes < 999);
		FC = MB_FC_NONE;
	}
}

void Modbus::WriteCoil()
{
	int Start;

	Start = word(ByteArray[8],ByteArray[9]);

	if (Start < (BUFFER_SIZE*BUFFER_SIZE))
	{
		pthread_mutex_lock(&bufferLock);
		if (bool_output[Start/8][Start%8] != NULL)
		{
			unsigned char value;
			if (word(ByteArray[10],ByteArray[11]) > 0)
			{
				value = 1;
			}
			else
			{
				value = 0;
			}
			*bool_output[Start/8][Start%8] = value;
		}
		pthread_mutex_unlock(&bufferLock);
	}
	else //invalid address
	{
		FC = MB_FC_ERROR;
		ER = ERR_ILLEGAL_DATA_ADDRESS;
	}

	if (FC == MB_FC_ERROR)
	{
		ModbusError();
	}
	else
	{
		ByteArray[5] = 2; //Number of bytes after this one.
		MessageLength = 12;
		Writes = 1 + Writes * (Writes < 999);
		FC = MB_FC_NONE;
	}
}

void Modbus::WriteRegister()
{
	int Start;

	Start = word(ByteArray[8],ByteArray[9]);

	if (Start < BUFFER_SIZE)
	{
		pthread_mutex_lock(&bufferLock);
		if (int_output[0][Start] != NULL) *int_output[0][Start] = word(ByteArray[10],ByteArray[11]);
		pthread_mutex_unlock(&bufferLock);
	}
	else //invalid address
	{
		FC = MB_FC_ERROR;
		ER = ERR_ILLEGAL_DATA_ADDRESS;
	}

	if (FC == MB_FC_ERROR)
	{
		ModbusError();
	}
	else
	{
		ByteArray[5] = 6; //Number of bytes after this one.
		MessageLength = 12;
		Writes = 1 + Writes * (Writes < 999);
		FC = MB_FC_NONE;
	}
}

void Modbus::WriteMultipleCoils()
{
	int Start, ByteDataLength, CoilDataLength;

	Start = word(ByteArray[8],ByteArray[9]);
	CoilDataLength = word(ByteArray[10],ByteArray[11]);
	ByteDataLength = CoilDataLength / 8;
	if(ByteDataLength * 8 < CoilDataLength) ByteDataLength++;
	CoilDataLength = ByteDataLength * 8;
	ByteArray[5] = ByteDataLength + 5; //Number of bytes after this one.

	pthread_mutex_lock(&bufferLock);
	for(int i = 0; i < ByteDataLength ; i++)
	{
		for(int j = 0; j < 8; j++)
		{
			int position = Start + i * 8 + j;
			if (position < (BUFFER_SIZE*BUFFER_SIZE))
			{
				if (bool_output[position/8][position%8] != NULL) *bool_output[position/8][position%8] = bitRead(ByteArray[13 + i], j);
			}
			else //invalid address
			{
				FC = MB_FC_ERROR;
				ER = ERR_ILLEGAL_DATA_ADDRESS;
			}
		}
	}
	pthread_mutex_unlock(&bufferLock);

	if (FC == MB_FC_ERROR)
	{
		ModbusError();
	}
	else
	{
		MessageLength = 12;
		Writes = 1 + Writes * (Writes < 999);
		FC = MB_FC_NONE;
	}
}

void Modbus::WriteMultipleRegisters()
{
	int Start, WordDataLength, ByteDataLength;

	Start = word(ByteArray[8],ByteArray[9]);
	WordDataLength = word(ByteArray[10],ByteArray[11]);
	ByteDataLength = WordDataLength * 2;
	ByteArray[5] = ByteDataLength + 3; //Number of bytes after this one.

	pthread_mutex_lock(&bufferLock);
	for(int i = 0; i < WordDataLength; i++)
	{
		if ((Start + i) < BUFFER_SIZE)
		{
			if (int_output[0][Start + i] != NULL) *int_output[0][Start + i] =  word(ByteArray[ 13 + i * 2],ByteArray[14 + i * 2]);
		}
		else //invalid address
		{
			FC = MB_FC_ERROR;
			ER = ERR_ILLEGAL_DATA_ADDRESS;
		}
	}
	pthread_mutex_unlock(&bufferLock);

	if (FC == MB_FC_ERROR)
	{
		ModbusError();
	}
	else
	{
		MessageLength = 12;
		Writes = 1 + Writes * (Writes < 999);
		FC = MB_FC_NONE;
	}
}

void Modbus::ModbusError()
{
	ByteArray[7] = ByteArray[7] | 0x80; //set the highest bit
	ByteArray[8] = ER;
	MessageLength = 9;
	FC = MB_FC_NONE;
	ER = ERR_NONE;
}

//-----------------------------------------------------------------------------
// Main method for this class. It must parse and process the client request
// and write back the response for it. The return value is the size of the
// response message in bytes.
//-----------------------------------------------------------------------------
int Modbus::Run(unsigned char *reply)
{
	Runs = 1 + Runs * (Runs < 999); //debug
	SetFC(ByteArray[7]);  //Byte 7 of request is FC

	//****************** Read Coils **********************
	if(FC == MB_FC_READ_COILS)
	{
		ReadCoils();
	}

	//*************** Read Discrete Inputs ***************
	else if(FC == MB_FC_READ_INPUTS)
	{
		ReadDiscreteInputs();
	}

	//****************** Read Holding Registers ******************
	else if(FC == MB_FC_READ_HOLDING_REGISTERS)
	{
		ReadHoldingRegisters();
	}

	//****************** Read Input Registers ******************
	else if(FC == MB_FC_READ_INPUT_REGISTERS)
	{
		ReadInputRegisters();
	}

	//****************** Write Coil **********************
	else if(FC == MB_FC_WRITE_COIL)
	{
		WriteCoil();
	}

	//****************** Write Register ******************
	else if(FC == MB_FC_WRITE_REGISTER)
	{
		WriteRegister();
	}

	//****************** Write Multiple Coils **********************
	else if(FC == MB_FC_WRITE_MULTIPLE_COILS)
	{
		WriteMultipleCoils();
	}

	//****************** Write Multiple Registers ******************
	else if(FC == MB_FC_WRITE_MULTIPLE_REGISTERS)
	{
		WriteMultipleRegisters();
	}

	//****************** Function Code Error ******************
	else if(FC == MB_FC_ERROR)
	{
		ModbusError();
	}

	for(int i = 0; i < MessageLength; i++)
	{
		reply[i] = ByteArray[i];
	}

	return MessageLength;
}
