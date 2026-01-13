/*
 * serial.c - Host Serial Port Abstraction Layer
 *
 * Provides interface to connect X68000 RS-232C port to host serial devices
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dirent.h>

#include "serial.h"

/* Global host serial instance */
HostSerial host_serial;

/* Serial device patterns to search */
static const char *serial_patterns[] = {
    "/dev/ttyS",
    "/dev/ttyUSB",
    "/dev/ttyACM",
    NULL
};

/* Convert baud rate to termios constant */
static speed_t baudrate_to_speed(int baudrate)
{
    switch (baudrate) {
        case 300:    return B300;
        case 600:    return B600;
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B9600;
    }
}

/* Initialize serial subsystem */
void Serial_Init(void)
{
    memset(&host_serial, 0, sizeof(host_serial));
    host_serial.fd = -1;
    host_serial.baudrate = 9600;
    host_serial.databits = 8;
    host_serial.stopbits = 1;
    host_serial.parity = SERIAL_PARITY_NONE;
    host_serial.is_open = 0;
}

/* Cleanup serial subsystem */
void Serial_Cleanup(void)
{
    Serial_Close();
}

/* Enumerate available serial devices */
int Serial_EnumDevices(char devices[][SERIAL_DEVICE_PATH_LEN], int max_devices)
{
    int count = 0;
    int i, j;
    char path[SERIAL_DEVICE_PATH_LEN];
    struct stat st;

    for (i = 0; serial_patterns[i] != NULL && count < max_devices; i++) {
        for (j = 0; j < 16 && count < max_devices; j++) {
            snprintf(path, sizeof(path), "%s%d", serial_patterns[i], j);

            /* Check if device exists and is a character device */
            if (stat(path, &st) == 0 && S_ISCHR(st.st_mode)) {
                /* Check if we have read/write access */
                if (access(path, R_OK | W_OK) == 0) {
                    strncpy(devices[count], path, SERIAL_DEVICE_PATH_LEN - 1);
                    devices[count][SERIAL_DEVICE_PATH_LEN - 1] = '\0';
                    count++;
                }
            }
        }
    }

    return count;
}

/* Open serial port */
int Serial_Open(const char *device)
{
    struct termios tty;

    /* Close if already open */
    if (host_serial.is_open) {
        Serial_Close();
    }

    /* Open device */
    host_serial.fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (host_serial.fd < 0) {
        printf("Serial: Failed to open %s: %s\n", device, strerror(errno));
        return -1;
    }

    /* Get current attributes */
    if (tcgetattr(host_serial.fd, &tty) != 0) {
        printf("Serial: tcgetattr failed: %s\n", strerror(errno));
        close(host_serial.fd);
        host_serial.fd = -1;
        return -1;
    }

    /* Set raw mode */
    cfmakeraw(&tty);

    /* Set default configuration */
    cfsetispeed(&tty, baudrate_to_speed(host_serial.baudrate));
    cfsetospeed(&tty, baudrate_to_speed(host_serial.baudrate));

    /* 8N1 default */
    tty.c_cflag &= ~PARENB;         /* No parity */
    tty.c_cflag &= ~CSTOPB;         /* 1 stop bit */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;             /* 8 data bits */
    tty.c_cflag |= CLOCAL | CREAD;  /* Enable receiver, ignore modem control */

    /* No flow control */
    tty.c_cflag &= ~CRTSCTS;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    /* Non-blocking read */
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    /* Apply settings */
    if (tcsetattr(host_serial.fd, TCSANOW, &tty) != 0) {
        printf("Serial: tcsetattr failed: %s\n", strerror(errno));
        close(host_serial.fd);
        host_serial.fd = -1;
        return -1;
    }

    /* Flush buffers */
    tcflush(host_serial.fd, TCIOFLUSH);

    /* Store device name */
    strncpy(host_serial.device, device, SERIAL_DEVICE_PATH_LEN - 1);
    host_serial.device[SERIAL_DEVICE_PATH_LEN - 1] = '\0';
    host_serial.is_open = 1;

    printf("Serial: Opened %s at %d baud\n", device, host_serial.baudrate);
    return 0;
}

/* Close serial port */
void Serial_Close(void)
{
    if (host_serial.fd >= 0) {
        /* Restore DTR/RTS to default */
        int flags;
        if (ioctl(host_serial.fd, TIOCMGET, &flags) == 0) {
            flags &= ~(TIOCM_DTR | TIOCM_RTS);
            ioctl(host_serial.fd, TIOCMSET, &flags);
        }

        close(host_serial.fd);
        printf("Serial: Closed %s\n", host_serial.device);
    }

    host_serial.fd = -1;
    host_serial.is_open = 0;
    host_serial.device[0] = '\0';
}

/* Check if serial port is open */
int Serial_IsOpen(void)
{
    return host_serial.is_open;
}

/* Get current device name */
const char* Serial_GetCurrentDevice(void)
{
    if (host_serial.is_open) {
        return host_serial.device;
    }
    return NULL;
}

/* Set baud rate */
int Serial_SetBaudRate(int baudrate)
{
    struct termios tty;

    host_serial.baudrate = baudrate;

    if (!host_serial.is_open) {
        return 0;
    }

    if (tcgetattr(host_serial.fd, &tty) != 0) {
        return -1;
    }

    cfsetispeed(&tty, baudrate_to_speed(baudrate));
    cfsetospeed(&tty, baudrate_to_speed(baudrate));

    if (tcsetattr(host_serial.fd, TCSANOW, &tty) != 0) {
        return -1;
    }

    printf("Serial: Baud rate set to %d\n", baudrate);
    return 0;
}

/* Set data bits */
int Serial_SetDataBits(int databits)
{
    struct termios tty;

    host_serial.databits = databits;

    if (!host_serial.is_open) {
        return 0;
    }

    if (tcgetattr(host_serial.fd, &tty) != 0) {
        return -1;
    }

    tty.c_cflag &= ~CSIZE;
    switch (databits) {
        case 5: tty.c_cflag |= CS5; break;
        case 6: tty.c_cflag |= CS6; break;
        case 7: tty.c_cflag |= CS7; break;
        case 8:
        default: tty.c_cflag |= CS8; break;
    }

    return tcsetattr(host_serial.fd, TCSANOW, &tty);
}

/* Set stop bits */
int Serial_SetStopBits(int stopbits)
{
    struct termios tty;

    host_serial.stopbits = stopbits;

    if (!host_serial.is_open) {
        return 0;
    }

    if (tcgetattr(host_serial.fd, &tty) != 0) {
        return -1;
    }

    if (stopbits == 2) {
        tty.c_cflag |= CSTOPB;
    } else {
        tty.c_cflag &= ~CSTOPB;
    }

    return tcsetattr(host_serial.fd, TCSANOW, &tty);
}

/* Set parity */
int Serial_SetParity(int parity)
{
    struct termios tty;

    host_serial.parity = parity;

    if (!host_serial.is_open) {
        return 0;
    }

    if (tcgetattr(host_serial.fd, &tty) != 0) {
        return -1;
    }

    switch (parity) {
        case SERIAL_PARITY_ODD:
            tty.c_cflag |= PARENB | PARODD;
            break;
        case SERIAL_PARITY_EVEN:
            tty.c_cflag |= PARENB;
            tty.c_cflag &= ~PARODD;
            break;
        case SERIAL_PARITY_NONE:
        default:
            tty.c_cflag &= ~PARENB;
            break;
    }

    return tcsetattr(host_serial.fd, TCSANOW, &tty);
}

/* Set all configuration at once */
int Serial_SetConfig(int baudrate, int databits, int stopbits, int parity)
{
    int ret = 0;
    ret |= Serial_SetBaudRate(baudrate);
    ret |= Serial_SetDataBits(databits);
    ret |= Serial_SetStopBits(stopbits);
    ret |= Serial_SetParity(parity);
    return ret;
}

/* Write single byte */
int Serial_Write(BYTE data)
{
    if (!host_serial.is_open) {
        return -1;
    }

    ssize_t n = write(host_serial.fd, &data, 1);
    return (n == 1) ? 0 : -1;
}

/* Read single byte (non-blocking) */
int Serial_Read(BYTE *data)
{
    if (!host_serial.is_open || data == NULL) {
        return -1;
    }

    ssize_t n = read(host_serial.fd, data, 1);
    if (n == 1) {
        return 0;
    } else if (n == 0 || (n < 0 && errno == EAGAIN)) {
        return 1;  /* No data available */
    }
    return -1;  /* Error */
}

/* Check bytes available for reading */
int Serial_BytesAvailable(void)
{
    int bytes = 0;

    if (!host_serial.is_open) {
        return 0;
    }

    if (ioctl(host_serial.fd, FIONREAD, &bytes) < 0) {
        return 0;
    }

    return bytes;
}

/* Set RTS line */
void Serial_SetRTS(int state)
{
    int flags;

    host_serial.rts = state;

    if (!host_serial.is_open) {
        return;
    }

    if (ioctl(host_serial.fd, TIOCMGET, &flags) < 0) {
        return;
    }

    if (state) {
        flags |= TIOCM_RTS;
    } else {
        flags &= ~TIOCM_RTS;
    }

    ioctl(host_serial.fd, TIOCMSET, &flags);
}

/* Set DTR line */
void Serial_SetDTR(int state)
{
    int flags;

    host_serial.dtr = state;

    if (!host_serial.is_open) {
        return;
    }

    if (ioctl(host_serial.fd, TIOCMGET, &flags) < 0) {
        return;
    }

    if (state) {
        flags |= TIOCM_DTR;
    } else {
        flags &= ~TIOCM_DTR;
    }

    ioctl(host_serial.fd, TIOCMSET, &flags);
}

/* Get CTS line state */
int Serial_GetCTS(void)
{
    int flags;

    if (!host_serial.is_open) {
        return 0;
    }

    if (ioctl(host_serial.fd, TIOCMGET, &flags) < 0) {
        return 0;
    }

    return (flags & TIOCM_CTS) ? 1 : 0;
}

/* Get DCD line state */
int Serial_GetDCD(void)
{
    int flags;

    if (!host_serial.is_open) {
        return 0;
    }

    if (ioctl(host_serial.fd, TIOCMGET, &flags) < 0) {
        return 0;
    }

    return (flags & TIOCM_CD) ? 1 : 0;
}

/* Get DSR line state */
int Serial_GetDSR(void)
{
    int flags;

    if (!host_serial.is_open) {
        return 0;
    }

    if (ioctl(host_serial.fd, TIOCMGET, &flags) < 0) {
        return 0;
    }

    return (flags & TIOCM_DSR) ? 1 : 0;
}

/* Get combined status byte */
BYTE Serial_GetStatus(void)
{
    BYTE status = 0;

    if (!host_serial.is_open) {
        return SERIAL_STAT_TXRDY;  /* TX always ready when not connected */
    }

    if (Serial_GetCTS()) status |= SERIAL_STAT_CTS;
    if (Serial_GetDCD()) status |= SERIAL_STAT_DCD;
    if (Serial_GetDSR()) status |= SERIAL_STAT_DSR;
    if (Serial_BytesAvailable() > 0) status |= SERIAL_STAT_RXRDY;
    status |= SERIAL_STAT_TXRDY;  /* Assume TX always ready for now */

    return status;
}

/* Send break signal */
void Serial_SendBreak(int duration_ms)
{
    if (!host_serial.is_open) {
        return;
    }

    /* tcsendbreak sends break for duration * 0.25-0.5 seconds */
    /* duration 0 means 0.25-0.5 seconds */
    int duration = (duration_ms > 0) ? (duration_ms / 250) : 0;
    tcsendbreak(host_serial.fd, duration);
}

/* Flush RX/TX buffers */
void Serial_Flush(int flags)
{
    if (!host_serial.is_open) {
        return;
    }

    int queue = TCIOFLUSH;
    if (flags == 1) queue = TCIFLUSH;      /* RX only */
    else if (flags == 2) queue = TCOFLUSH; /* TX only */

    tcflush(host_serial.fd, queue);
}

/* Get current baud rate */
int Serial_GetBaudRate(void)
{
    return host_serial.baudrate;
}

/* Get current configuration */
void Serial_GetConfig(int *baudrate, int *databits, int *stopbits, int *parity)
{
    if (baudrate) *baudrate = host_serial.baudrate;
    if (databits) *databits = host_serial.databits;
    if (stopbits) *stopbits = host_serial.stopbits;
    if (parity) *parity = host_serial.parity;
}
