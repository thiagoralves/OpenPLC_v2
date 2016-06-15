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
#include <pthread.h>
#include <modbus.h>
#include <errno.h>
#include <string.h>

#include "ladder.h"

#define MB_TCP				1
#define MB_RTU				2
#define MAX_MB_IO			400

uint8_t bool_input_buf[MAX_MB_IO];
uint8_t bool_output_buf[MAX_MB_IO];
uint16_t int_input_buf[MAX_MB_IO];
uint16_t int_output_buf[MAX_MB_IO];

pthread_mutex_t ioLock;

struct MB_address
{
	uint16_t start_address;
	uint16_t num_regs;
};

struct MB_device
{
	modbus_t *mb_ctx;
	char dev_name[100];
	uint8_t protocol;
	char dev_address[100];
	uint16_t ip_port;
	int rtu_baud;
	char rtu_parity;
	int rtu_data_bit;
	int rtu_stop_bit;
	uint8_t dev_id;
	bool isConnected;

	struct MB_address discrete_inputs;
	struct MB_address coils;
	struct MB_address input_registers;
	struct MB_address holding_registers;
};

struct MB_device *mb_devices;
uint8_t num_devices;

//-----------------------------------------------------------------------------
// Helper function - Makes the running thread sleep for the ammount of time
// in milliseconds
//-----------------------------------------------------------------------------
void sleep_ms(int milliseconds)
{
	struct timespec ts;
	ts.tv_sec = milliseconds / 1000;
	ts.tv_nsec = (milliseconds % 1000) * 1000000;
	nanosleep(&ts, NULL);
}

void parseConfig()
{
	num_devices = 2;
	mb_devices = (struct MB_device *)malloc(num_devices*sizeof(struct MB_device));

	strcpy(mb_devices[0].dev_name, "MB_dev01");
	mb_devices[0].protocol = MB_TCP;
	strcpy(mb_devices[0].dev_address, "192.168.23.1");
	mb_devices[0].ip_port = 502;
	mb_devices[0].dev_id = 1;
	mb_devices[0].isConnected = false;

	mb_devices[0].discrete_inputs.start_address = 0;
	mb_devices[0].discrete_inputs.num_regs = 8;
	mb_devices[0].coils.start_address = 0;
	mb_devices[0].coils.num_regs = 0;
	mb_devices[0].input_registers.start_address = 0;
	mb_devices[0].input_registers.num_regs = 10;
	mb_devices[0].holding_registers.start_address = 0;
	mb_devices[0].holding_registers.num_regs = 0;

	strcpy(mb_devices[1].dev_name, "MB_dev02");
	mb_devices[1].protocol = MB_TCP;
	strcpy(mb_devices[1].dev_address, "192.168.23.1");
	mb_devices[1].ip_port = 503;
	mb_devices[1].dev_id = 1;
	mb_devices[1].isConnected = false;

	mb_devices[1].discrete_inputs.start_address = 0;
	mb_devices[1].discrete_inputs.num_regs = 0;
	mb_devices[1].coils.start_address = 0;
	mb_devices[1].coils.num_regs = 8;
	mb_devices[1].input_registers.start_address = 0;
	mb_devices[1].input_registers.num_regs = 0;
	mb_devices[1].holding_registers.start_address = 0;
	mb_devices[1].holding_registers.num_regs = 10;
}

void *exchangeData(void *arg)
{
	while(1)
	{
		uint16_t bool_input_index = 0;
		uint16_t bool_output_index = 0;
		uint16_t int_input_index = 0;
		uint16_t int_output_index = 0;

		for (int i = 0; i < num_devices; i++)
		{
			//Verify if device is connected
			if (!mb_devices[i].isConnected)
			{
				if (modbus_connect(mb_devices[i].mb_ctx) == -1)
				{
					printf("Connection failed on MB device %s: %s\n", mb_devices[i].dev_name, modbus_strerror(errno));
				}
				else
				{
					mb_devices[i].isConnected = true;
				}
			}

			if (mb_devices[i].isConnected)
			{
				//Read discrete inputs
				if (mb_devices[i].discrete_inputs.num_regs != 0)
				{
					uint8_t *tempBuff;
					tempBuff = (uint8_t *)malloc(mb_devices[i].discrete_inputs.num_regs);
					int return_val = modbus_read_input_bits(mb_devices[i].mb_ctx, mb_devices[i].discrete_inputs.start_address,
															mb_devices[i].discrete_inputs.num_regs, tempBuff);
					if (return_val == -1)
					{
						modbus_close(mb_devices[i].mb_ctx);
						mb_devices[i].isConnected = false;
					}
					else
					{
						pthread_mutex_lock(&ioLock);
						for (int j = 0; j < return_val; j++)
						{
							bool_input_buf[bool_input_index] = tempBuff[j];
							bool_input_index++;
						}
						pthread_mutex_unlock(&ioLock);
					}

					free(tempBuff);
				}

				//Write coils
				if (mb_devices[i].coils.num_regs != 0)
				{
					uint8_t *tempBuff;
					tempBuff = (uint8_t *)malloc(mb_devices[i].coils.num_regs);

					pthread_mutex_lock(&ioLock);
					for (int j = 0; j < mb_devices[i].coils.num_regs; j++)
					{
						tempBuff[j] = bool_output_buf[bool_output_index];
						bool_output_index++;
					}
					pthread_mutex_unlock(&ioLock);

					int return_val = modbus_write_bits(mb_devices[i].mb_ctx, mb_devices[i].coils.start_address, mb_devices[i].coils.num_regs, tempBuff);
					if (return_val == -1)
					{
						modbus_close(mb_devices[i].mb_ctx);
						mb_devices[i].isConnected = false;
					}
				}

				//Read input registers
				if (mb_devices[i].input_registers.num_regs != 0)
				{
					uint16_t *tempBuff;
					tempBuff = (uint16_t *)malloc(2*mb_devices[i].input_registers.num_regs);
					int return_val = modbus_read_input_registers(	mb_devices[i].mb_ctx, mb_devices[i].input_registers.start_address,
																	mb_devices[i].input_registers.num_regs, tempBuff);
					if (return_val == -1)
					{
						modbus_close(mb_devices[i].mb_ctx);
						mb_devices[i].isConnected = false;
					}
					else
					{
						pthread_mutex_lock(&ioLock);
						for (int j = 0; j < return_val; j++)
						{
							int_input_buf[int_input_index] = tempBuff[j];
							int_input_index++;
						}
						pthread_mutex_unlock(&ioLock);
					}

					free(tempBuff);
				}

				//Write holding registers
				if (mb_devices[i].holding_registers.num_regs != 0)
				{
					uint16_t *tempBuff;
					tempBuff = (uint16_t *)malloc(2*mb_devices[i].holding_registers.num_regs);

					pthread_mutex_lock(&ioLock);
					for (int j = 0; j < mb_devices[i].holding_registers.num_regs; j++)
					{
						tempBuff[j] = int_output_buf[int_output_index];
						int_output_index++;
					}
					pthread_mutex_unlock(&ioLock);

					int return_val = modbus_write_registers(mb_devices[i].mb_ctx, mb_devices[i].holding_registers.start_address,
															mb_devices[i].holding_registers.num_regs, tempBuff);
					if (return_val == -1)
					{
						modbus_close(mb_devices[i].mb_ctx);
						mb_devices[i].isConnected = false;
					}
				}
			}
		}

		sleep_ms(30);
	}
}

//-----------------------------------------------------------------------------
// This function is called by the main OpenPLC routine when it is initializing.
// Hardware initialization procedures should be here.
//-----------------------------------------------------------------------------
void initializeHardware()
{
	parseConfig();

	for (int i = 0; i < num_devices; i++)
	{
		if (mb_devices[i].protocol == MB_TCP)
		{
			mb_devices[i].mb_ctx = modbus_new_tcp(mb_devices[i].dev_address, mb_devices[i].ip_port);
		}
		else if (mb_devices[i].protocol == MB_RTU)
		{
			mb_devices[i].mb_ctx = modbus_new_rtu(	mb_devices[i].dev_address, mb_devices[i].rtu_baud,
													mb_devices[i].rtu_parity, mb_devices[i].rtu_data_bit,
													mb_devices[i].rtu_stop_bit);
		}

		modbus_set_slave(mb_devices[i].mb_ctx, mb_devices[i].dev_id);
	}

	pthread_t thread;
	pthread_create(&thread, NULL, exchangeData, NULL);
}

//-----------------------------------------------------------------------------
// This function is called by the OpenPLC in a loop. Here the internal buffers
// must be updated to reflect the actual I/O state. The mutex bufferLock
// must be used to protect access to the buffers on a threaded environment.
//-----------------------------------------------------------------------------
void updateBuffers()
{
	pthread_mutex_lock(&bufferLock); //lock mutex
	pthread_mutex_lock(&ioLock);

	for (int i = 0; i < 10; i++)
	{
		if (bool_input[i/8][i%8] != NULL) *bool_input[i/8][i%8] = bool_input_buf[i];
		if (int_input[0][i] != NULL) *int_input[0][i] = int_input_buf[i];
		if (bool_output[i/8][i%8] != NULL) bool_output_buf[i] = *bool_output[i/8][i%8];
		if (int_output[i/8][i%8] != NULL) int_output_buf[i] = *int_output[i/8][i%8];
	}

	pthread_mutex_unlock(&ioLock);
	pthread_mutex_unlock(&bufferLock); //unlock mutex
}
