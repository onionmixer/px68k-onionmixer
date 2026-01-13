/*
 * serial.h - Host Serial Port Abstraction Layer
 *
 * Provides interface to connect X68000 RS-232C port to host serial devices
 * (e.g., /dev/ttyUSB0, /dev/ttyS0, /dev/ttyACM0)
 */

#ifndef _X68K_SERIAL_H_
#define _X68K_SERIAL_H_

#include "common.h"

#define MAX_SERIAL_DEVICES  16
#define SERIAL_DEVICE_PATH_LEN 64

/* Parity settings */
#define SERIAL_PARITY_NONE  0
#define SERIAL_PARITY_ODD   1
#define SERIAL_PARITY_EVEN  2

/* Serial port status bits */
#define SERIAL_STAT_CTS     0x01    /* Clear To Send */
#define SERIAL_STAT_DCD     0x02    /* Data Carrier Detect */
#define SERIAL_STAT_DSR     0x04    /* Data Set Ready */
#define SERIAL_STAT_RXRDY   0x10    /* RX data available */
#define SERIAL_STAT_TXRDY   0x20    /* TX buffer empty */

/* Host serial port structure */
typedef struct {
    int fd;                                 /* File descriptor (-1 if closed) */
    char device[SERIAL_DEVICE_PATH_LEN];    /* Device path */
    int baudrate;                           /* Current baud rate */
    int databits;                           /* 5, 6, 7, 8 */
    int stopbits;                           /* 1, 2 */
    int parity;                             /* SERIAL_PARITY_* */
    int rts;                                /* RTS line state (output) */
    int dtr;                                /* DTR line state (output) */
    int is_open;                            /* Connection status */
} HostSerial;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialization and cleanup */
void Serial_Init(void);
void Serial_Cleanup(void);

/* Device enumeration */
int Serial_EnumDevices(char devices[][SERIAL_DEVICE_PATH_LEN], int max_devices);

/* Connection management */
int  Serial_Open(const char *device);
void Serial_Close(void);
int  Serial_IsOpen(void);
const char* Serial_GetCurrentDevice(void);

/* Configuration */
int Serial_SetBaudRate(int baudrate);
int Serial_SetDataBits(int databits);
int Serial_SetStopBits(int stopbits);
int Serial_SetParity(int parity);
int Serial_SetConfig(int baudrate, int databits, int stopbits, int parity);

/* Data transfer */
int Serial_Write(BYTE data);
int Serial_Read(BYTE *data);
int Serial_BytesAvailable(void);

/* Line control */
void Serial_SetRTS(int state);
void Serial_SetDTR(int state);
int  Serial_GetCTS(void);
int  Serial_GetDCD(void);
int  Serial_GetDSR(void);
BYTE Serial_GetStatus(void);

/* Break and flush */
void Serial_SendBreak(int duration_ms);
void Serial_Flush(int flags);  /* 0=both, 1=RX, 2=TX */

/* Query configuration */
int  Serial_GetBaudRate(void);
void Serial_GetConfig(int *baudrate, int *databits, int *stopbits, int *parity);

/* External variables */
extern HostSerial host_serial;

#ifdef __cplusplus
}
#endif

#endif /* _X68K_SERIAL_H_ */
