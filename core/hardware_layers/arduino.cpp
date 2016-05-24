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

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>

#include "ladder.h"

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) (bitvalue ? bitSet(value, bit) : bitClear(value, bit))

struct OPLC_input
{
	uint8_t digital[4];
	uint16_t analog[16];
};

struct OPLC_output
{
	uint8_t digital[2];
	uint16_t analog[12];
};

struct OPLC_input input_data;
struct OPLC_output output_data;

int serial_fd;

pthread_mutex_t ioLock;

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

//-----------------------------------------------------------------------------
// Takes the string name of the serial port (e.g. "/dev/tty.usbserial","COM1")
// and a baud rate (bps) and connects to that port at that speed and 8N1.
// Opens the port in fully raw mode so you can send binary data. Returns valid
// fd, or -1 on error
//-----------------------------------------------------------------------------
int serialport_init(const char* serialport, int baud)
{
    struct termios toptions;
    int fd;

    fd = open(serialport, O_RDWR | O_NONBLOCK );

    if (fd == -1)
    {
        perror("serialport_init: Unable to open port ");
        return -1;
    }

    if (tcgetattr(fd, &toptions) < 0)
    {
        perror("serialport_init: Couldn't get term attributes");
        return -1;
    }

    speed_t brate = baud; // let you override switch below if needed
    switch(baud) {
    case 4800:   brate=B4800;   break;
    case 9600:   brate=B9600;   break;
#ifdef B14400
    case 14400:  brate=B14400;  break;
#endif
    case 19200:  brate=B19200;  break;
#ifdef B28800
    case 28800:  brate=B28800;  break;
#endif
    case 38400:  brate=B38400;  break;
    case 57600:  brate=B57600;  break;
    case 115200: brate=B115200; break;
    }
    cfsetispeed(&toptions, brate);
    cfsetospeed(&toptions, brate);

    // 8N1
    toptions.c_cflag &= ~PARENB;
    toptions.c_cflag &= ~CSTOPB;
    toptions.c_cflag &= ~CSIZE;
    toptions.c_cflag |= CS8;
    // no flow control
    toptions.c_cflag &= ~CRTSCTS;

    //toptions.c_cflag &= ~HUPCL; // disable hang-up-on-close to avoid reset

    toptions.c_cflag |= CREAD | CLOCAL;  // turn on READ & ignore ctrl lines
    toptions.c_iflag &= ~(IXON | IXOFF | IXANY); // turn off s/w flow ctrl

    toptions.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // make raw
    toptions.c_oflag &= ~OPOST; // make raw

    // see: http://unixwiz.net/techtips/termios-vmin-vtime.html
    toptions.c_cc[VMIN]  = 0;
    toptions.c_cc[VTIME] = 0;
    //toptions.c_cc[VTIME] = 20;

    tcsetattr(fd, TCSANOW, &toptions);
    if( tcsetattr(fd, TCSAFLUSH, &toptions) < 0)
    {
        perror("init_serialport: Couldn't set term attributes");
        return -1;
    }

    return fd;
}

//-----------------------------------------------------------------------------
// Send a packet to the IO board
//-----------------------------------------------------------------------------
void sendPacket()
{
	uint8_t temp[100];
	uint8_t outgoingBuffer[100];
	struct OPLC_output *dataPointer;
	dataPointer = &output_data;

	pthread_mutex_lock(&ioLock);
	memcpy(temp, dataPointer, sizeof(struct OPLC_output));
	pthread_mutex_unlock(&ioLock);

	outgoingBuffer[0] = 'S';
	int j = 1;

	for (int i = 0; i < sizeof(struct OPLC_output); i++)
	{
		if (temp[i] != 'S' && temp[i] != 'E' && temp[i] != '\\')
		{
			outgoingBuffer[j] = temp[i];
			j++;
		}
		else
		{
			outgoingBuffer[j] = '\\';
			j++;
			outgoingBuffer[j] = temp[i];
			j++;
		}
	}
	outgoingBuffer[j] = 'E';
	j++;

	/*
	printf("Packet Sent: ");
	for (int i = 0; i < j; i++)
	{
		printf("0x%02x ", outgoingBuffer[i]);
	}
	printf("\n");
	*/

	write(serial_fd, outgoingBuffer, j);
}

//-----------------------------------------------------------------------------
// Verify the buffer received and remove the byte escapes. Returns TRUE if
// successfull or FALSE in case of error.
//-----------------------------------------------------------------------------
bool parseBuffer(uint8_t *buf, int bufSize)
{
	bool beginReceiving = false;
	bool escapeReceived = false;
	bool packetReceived = false;
	int bufferIndex = 0;

	for (int i = 0; i < bufSize; i++)
	{
		if (!beginReceiving && buf[i] == 'S')
		{
			beginReceiving = true;
		}

		else if (beginReceiving)
		{
			if (!escapeReceived)
			{
				if (buf[i] == '\\')
				{
					escapeReceived = true;
				}
				else if (buf[i] == 'E')
				{
					//End packet
					escapeReceived = false;
					beginReceiving = false;
					packetReceived = true;
					bufferIndex = 0;

				}
				else if (buf[i] == 'S')
				{
					//Missed end of last packet. Drop packet and start a new one
					escapeReceived = false;
					beginReceiving = true;
					packetReceived = false;
					bufferIndex = 0;
				}
				else
				{
					buf[bufferIndex] = buf[i];
					bufferIndex++;
				}
			}

			else if (escapeReceived)
			{
				if (buf[i] == '\\' || buf[i] == 'E' || buf[i] == 'S')
				{
					buf[bufferIndex] = buf[i];
					bufferIndex++;
					escapeReceived = false;
				}
				else
				{
					//Invalid sequence! Drop packet
					escapeReceived = false;
					beginReceiving = false;
					packetReceived = false;
					bufferIndex = 0;
				}
			}
		}
	}

	return packetReceived;
}

//-----------------------------------------------------------------------------
// Receive a packet from the IO board
//-----------------------------------------------------------------------------
void receivePacket()
{
	uint8_t receiveBuffer[100];
	int response = read(serial_fd, receiveBuffer, 100);
	if (response == -1)
	{
		printf("Couldn't read from IO\n");
	}
	else if (response == 0)
	{
		printf("No response from IO\n");
	}
	else
	{
		/*
		printf("Read %d bytes\n", response);
		for (int i = 0; i < response; i++)
		{
			printf("0x%02x ", receiveBuffer[i]);
			receiveBuffer[i] = 0;
		}
		printf("\n");
		*/

		if (parseBuffer(receiveBuffer, response))
		{
			struct OPLC_input *dataPointer;
			dataPointer = &input_data;

			pthread_mutex_lock(&ioLock);
			memcpy(dataPointer, receiveBuffer, sizeof(struct OPLC_input));
			pthread_mutex_unlock(&ioLock);
		}
	}
}

//-----------------------------------------------------------------------------
// Thread to send and receive data from the IO board
//-----------------------------------------------------------------------------
void *exchangeData(void *arg)
{
	while(1)
	{
		sendPacket();
		sleep_ms(1);
		receivePacket();

		sleep_ms(30);
	}
}

//-----------------------------------------------------------------------------
// This function is called by the main OpenPLC routine when it is initializing.
// Hardware initialization procedures should be here.
//-----------------------------------------------------------------------------
void initializeHardware()
{
	serial_fd = serialport_init("/dev/ttyACM0", 115200);
	sleep_ms(100);
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
	//Lock mutexes
	pthread_mutex_lock(&bufferLock);
	pthread_mutex_lock(&ioLock);

	//Digital Input
	for (int i = 0; i < 8; i++)
	{
		if (bool_input[0][i] != NULL) *bool_input[0][i] = bitRead(input_data.digital[0], i);
	}

	//Analog Input
	for (int i = 0; i < 6; i++)
	{
		if (int_input[0][i] != NULL) *int_input[0][i] = input_data.analog[i];
	}

	//Digital Output
	for (int i = 0; i < 8; i++)
	{
		if (bool_output[0][i] != NULL) bitWrite(output_data.digital[0], i, *bool_output[0][i]);
	}

	//Analog Output
	for (int i = 0; i < 4; i++)
	{
		if (int_output[0][i] != NULL) output_data.analog[i] = *int_output[0][i];
	}

	pthread_mutex_unlock(&ioLock);
	pthread_mutex_unlock(&bufferLock);
}
