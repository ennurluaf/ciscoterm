/*
 * serial.c - Serial console communication via termios
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

#include "serial.h"

/* ------------------------------------------------------------------ */
/* Baud rate helper                                                      */
/* ------------------------------------------------------------------ */

speed_t serial_baud_const(int baud) {
    switch (baud) {
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
        default:
            fprintf(stderr, "serial: unknown baud %d, using 9600\n", baud);
            return B9600;
    }
}

/* ------------------------------------------------------------------ */
/* Open                                                                  */
/* ------------------------------------------------------------------ */

int serial_open(SerialConn *s, const char *path, int baud) {
    memset(s, 0, sizeof(*s));
    s->fd = -1;

    strncpy(s->device_path, path, SERIAL_PATH_LEN - 1);
    s->baud = baud;

    /* O_NOCTTY: don't make it our controlling terminal
     * O_NONBLOCK: don't block waiting for DCD */
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("serial_open: open");
        return -1;
    }

    /* Save original terminal attributes */
    if (tcgetattr(fd, &s->saved_tty) != 0) {
        perror("serial_open: tcgetattr");
        close(fd);
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    speed_t speed = serial_baud_const(baud);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    /* 8N1, no flow control, raw mode */
    tty.c_cflag  = (tty.c_cflag & ~CSIZE) | CS8;  /* 8-bit chars */
    tty.c_cflag |= (CLOCAL | CREAD);               /* enable reading */
    tty.c_cflag &= ~(PARENB | PARODD);             /* no parity */
    tty.c_cflag &= ~CSTOPB;                        /* 1 stop bit */
    tty.c_cflag &= ~CRTSCTS;                       /* no HW flow control */

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);        /* no SW flow control */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK |
                     ISTRIP | INLCR  | IGNCR | ICRNL);

    tty.c_lflag  = 0;                              /* raw, no echo */
    tty.c_oflag  = 0;                              /* no post-processing */

    /* Non-blocking read: return immediately with 0 bytes if nothing */
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1; /* 0.1s timeout */

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("serial_open: tcsetattr");
        close(fd);
        return -1;
    }

    /* Flush any junk in the buffers */
    tcflush(fd, TCIOFLUSH);

    s->fd = fd;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Close                                                                 */
/* ------------------------------------------------------------------ */

void serial_close(SerialConn *s) {
    if (s->fd > 0) {
        tcsetattr(s->fd, TCSANOW, &s->saved_tty);
        close(s->fd);
        s->fd = -1;
    }
    s->device_path[0] = '\0';
}

/* ------------------------------------------------------------------ */
/* Read (non-blocking)                                                   */
/* ------------------------------------------------------------------ */

int serial_read(SerialConn *s, char *buf, int maxlen) {
    if (s->fd <= 0) return -1;

    int n = read(s->fd, buf, maxlen);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Write                                                                  */
/* ------------------------------------------------------------------ */

int serial_write(SerialConn *s, const char *buf, int len) {
    if (s->fd <= 0) return -1;

    int written = 0;
    while (written < len) {
        int n = write(s->fd, buf + written, len - written);
        if (n < 0) {
            if (errno == EAGAIN) {
                usleep(1000);
                continue;
            }
            return -1;
        }
        written += n;
    }
    return written;
}
