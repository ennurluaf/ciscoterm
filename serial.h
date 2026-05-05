/*
 * serial.h - Serial console communication via termios
 */

#ifndef SERIAL_H
#define SERIAL_H

#include <termios.h>

#define SERIAL_PATH_LEN 64

typedef struct {
    int  fd;
    char device_path[SERIAL_PATH_LEN];
    struct termios saved_tty;   /* original terminal state to restore */
    int  baud;
} SerialConn;

/**
 * Open a serial device at the given baud rate.
 * Returns 0 on success, -1 on error (check errno).
 */
int  serial_open(SerialConn *s, const char *path, int baud);

/**
 * Close the serial connection and restore terminal state.
 */
void serial_close(SerialConn *s);

/**
 * Non-blocking read. Returns bytes read, 0 if nothing available, -1 on error.
 */
int  serial_read(SerialConn *s, char *buf, int maxlen);

/**
 * Write buf[0..len-1] to the serial port. Returns bytes written or -1.
 */
int  serial_write(SerialConn *s, const char *buf, int len);

/**
 * Convert an integer baud rate (e.g. 9600) to a termios speed_t constant.
 * Returns B9600 as a safe default if the rate isn't recognised.
 */
speed_t serial_baud_const(int baud);

#endif /* SERIAL_H */
