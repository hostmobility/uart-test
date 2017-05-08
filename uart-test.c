/**
 *
 * uart-test - program that is able to send/recieve data on a serial port
 *
 * Copyright (C) 2017 Host Mobility AB 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>


#ifndef TRUE
#define TRUE (1==1)
#endif

#ifndef FALSE
#define FALSE (!TRUE)
#endif

#define UART_TEST_BLOCK_SIZE 255     
                 
/* local types */
typedef enum {receive, send} direction_t;

/* local functions */ 
/**  Local cleanup function called at exit */                                               
static void cleanup(void);      
/**  Signal handler for error signal */   
void signalTerminate(int signo);
static void usage(void);
                     
int serPortFd = -1;

FILE *binFileFp = NULL;

int sendBytes(int fd, uint8_t * txBufPtr, int length)
{
    int txLength = 0;
    uint8_t * bufPtr = NULL;
    int result;
    
    /* sanity check */
    if(length <= 0)
    {
        return 0;
    }
    
    txLength = length;
    bufPtr = txBufPtr;
   
    /* transmit the full content  */
    do
    {
        result = write(fd, bufPtr, txLength);
        if(result > 0) 
        {
            bufPtr = bufPtr + (txLength - result);
            txLength -= result;
        }
        else if (result == 0)
        {
            usleep(20000);
        }
        else
        {
            perror("Write error");
            return (0);
        }
    } 
    while (txLength > 0);
    
    return(length);
}

int receiveBytes(int fd, uint8_t * rxBufPtr, int length)
{
    int rxLength = 0;
    uint8_t * bufPtr = NULL;
    int result;
    
    /* sanity check */
    if(length <= 0)
    {
        return 0;
    }
    
    bufPtr = rxBufPtr;
   
    /* read bytes */
    do
    {
        result = read(fd, bufPtr, length - rxLength);
        if(result > 0) 
        {
           
            bufPtr += result;
            rxLength += result;

        }
        else if (result == 0)
        {
            usleep(20000);
        }
        else
        {
            perror("Read error");
            return (0);
        }
    } 
    while (rxLength < length);
    
    return(rxLength);
}

/**  main - Command line arguments: send/receive port, baudrate, file to send/receive,
     bytecount (optional for send, mandatory for receive)
     Return -1 on error, never returns if everything goes OK. */
int main(int argc, char ** argv)
{                      
    uint8_t rxBuf [UART_TEST_BLOCK_SIZE + 1];
    uint8_t txBuf [UART_TEST_BLOCK_SIZE + 1];    
    struct termios t;              
    errno = 0;
    char portName [255] = "/dev/ttyS0";
    direction_t direction = send;
    int baudflag;
    char fileName [255];
    int i;
    int byteCount = 0;
    int sentBytes = 0;
    int receivedBytes = 0;
    int chunk = 0;
    const int blockSize = UART_TEST_BLOCK_SIZE;
    char txchar;
    char rxchar;

    /* Get arguments */
    if (argc < 5)
    {
        fprintf(stderr, "Wrong number of arguments!\n");
        usage();
        exit(EXIT_FAILURE);       
    }


    /* send/receive */
    if(argv[1][0] == 's')
    {
        direction = send;
    }
    else if (argv[1][0] == 'r')
    {
        direction = receive;
    }
    else
    {
        fprintf(stderr, "You must specify send or receive!\n\n");
        usage();
        exit (EXIT_FAILURE);       
    }

    /* port name */
    strncpy (portName, argv[2], sizeof(portName) - 1);     

    switch(atoi(argv[3])) {
        case 1200: baudflag = B1200;
        break;

        case 2400: baudflag = B2400;
        break;

        case 4800: baudflag = B4800;
        break;

        case 9600: baudflag = B9600;
        break;

        case 19200: baudflag = B19200;
        break;

        case 38400: baudflag = B38400;
        break;

        case 57600: baudflag = B57600;
        break;

        case 115200: baudflag = B115200;
        break;

        default:
        fprintf(stderr, "Unsupported baudrate!\n\n");
        usage();
        exit(EXIT_FAILURE);            

    }


    /* File name */
    strncpy (fileName, argv[4], sizeof(fileName) - 1); 

    if(argc > 5) byteCount = atoi(argv[5]);

   /* 
     * Set up signal handlers to act on kill and CTRL-C events 
     */
    if (signal(SIGTERM, signalTerminate) == SIG_ERR)
    {
        perror("Can't register signal handler ");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGINT, signalTerminate) == SIG_ERR)
    {
        perror("Can't register signal handler for CTRL-C et al");
        exit(EXIT_FAILURE);
    }
    if (atexit(cleanup) != 0)
    {
        perror("Can't register exit handler\n");
        exit(EXIT_FAILURE);
    }  

    /* Set up ports */
    if ((serPortFd = open(portName, O_RDWR | O_NOCTTY )) < 0) 
    {  
        perror("Unable to open serial port");
        exit(EXIT_FAILURE);
    }



    /* Open binary file */
    if(direction == receive)
    {
        binFileFp = fopen(fileName, "wb");
    }
    else
    {
        binFileFp = fopen(fileName, "rb");
    }


    if (binFileFp == NULL) 
    {  
        perror("Unable to open binary file");
        exit(EXIT_FAILURE);
    }

    /* Get bytes to send if not specified */
    if((byteCount == 0) && (direction == send))
    {
        fseek(binFileFp, 0L, SEEK_END);
        byteCount = ftell(binFileFp);
        fseek(binFileFp, 0L, SEEK_SET);
    }   

    /* Setup baudrate. */
    if(tcgetattr(serPortFd, &t))
    {
        perror("Unable to get termios attributes");
        exit(EXIT_FAILURE);
    }

    cfmakeraw(&t);   
    t.c_iflag = IGNBRK | IGNPAR | INPCK; 
    t.c_cflag = baudflag | CS8 | CREAD | CLOCAL;

    if(tcsetattr(serPortFd, TCSANOW, &t))
    {
        perror("Unable to set termios attributes");
        exit(EXIT_FAILURE);
    }

    /* flush buffers */   
    tcflush(serPortFd, TCIFLUSH);

    if (direction == send)
    {
        printf("Will now send %d bytes from file %s on port %s\n", byteCount, fileName, portName);
    }
    else
    {
        printf("Will now receive %d bytes to file %s on port %s\n", byteCount, fileName, portName);
    }    


    /* Send mode */
    if (direction == send)
    {
        while ((sentBytes < byteCount) && (byteCount > 0 ))
        {
            if((byteCount - sentBytes) > blockSize)
            {
                chunk = blockSize;
            }
            else
            {
                chunk = (byteCount - sentBytes);
            }

            for (i = 0; i< chunk; i++)
            {
                txchar = getc(binFileFp);
                txBuf[i] = txchar;
            } 

            sentBytes += sendBytes(serPortFd, txBuf, chunk);

        }
        /* Make sure all bytes are sent */
        tcdrain(serPortFd);
        printf("Sent %d bytes - Finished\n", sentBytes);
    }
    else /* receive */
    {
        while ((receivedBytes < byteCount) && (byteCount > 0 ))
        {
            if((byteCount - receivedBytes) > blockSize)
            {
                chunk = blockSize;
            }
            else
            {
                chunk = (byteCount - receivedBytes);
            }

            receivedBytes += receiveBytes(serPortFd, rxBuf, chunk);

            for (i = 0; i< chunk; i++)
            {
                rxchar = rxBuf[i];
                fputc(rxchar, binFileFp);
            } 



        }
        printf("Received %d bytes - Finished\n", receivedBytes);
    }
    exit(EXIT_SUCCESS);
    
}            

static void cleanup(void)
{
    if(serPortFd > 0)
    {
        close(serPortFd);
    }
    if(binFileFp != NULL)
    {
        fclose(binFileFp);
    }
}

static void usage(void)
{
    printf("uart-test <send|receive> <port> <baudrate> <file> <byteCount>\n\n");
}

void signalTerminate(int signo)
{
    /*
    * This will force the exit handler to run
    */   
    exit(EXIT_FAILURE);
}
  
