/*
 * uart.h -- uart utils.
 *
 * Copyright (c) zhoukk <izhoukk@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _UART_H_
#define _UART_H_

#ifdef __cplusplus
extern "C" {
#endif

/* generic includes. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#if defined(__GNUC__) && (__GNUC__ >= 4)
#define UART_API __attribute__((visibility("default")))
#else
#define UART_API
#endif

extern UART_API int uart_open(const char *dev, int baude, int c_flow, int bits, char parity, int stop);

extern UART_API void uart_close(int fd);

extern UART_API int uart_write(int fd, const char *data, size_t len);

extern UART_API int uart_read(int fd, char *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* _UART_H_ */

#ifdef UART_IMPLEMENTATION

int
uart_open(const char *dev, int baude, int c_flow, int bits, char parity, int stop) {
    int fd;
    struct termios options;
    int speed;

    fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, 0) < 0) {
        goto e;
    }

    if (tcgetattr(fd, &options) < 0) {
        goto e;
    }

    switch (baude) {
    case 4800:
        speed = B4800;
        break;
    case 9600:
        speed = B9600;
        break;
    case 19200:
        speed = B19200;
        break;
    case 38400:
        speed = B38400;
        break;
    case 115200:
        speed = B115200;
        break;
    default:
        goto e;
    }

    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);

    options.c_cflag |= (CLOCAL | CREAD);

    switch (c_flow) {
    case 0:
        options.c_cflag &= ~CRTSCTS;
        break;
    case 1:
        options.c_cflag |= CRTSCTS;
        break;
    case 2:
        options.c_cflag |= IXON | IXOFF | IXANY;
        break;
    default:
        goto e;
    }

    options.c_cflag &= ~CSIZE;
    switch (bits) {
    case 5:
        options.c_cflag |= CS5;
        break;
    case 6:
        options.c_cflag |= CS6;
        break;
    case 7:
        options.c_cflag |= CS7;
        break;
    case 8:
        options.c_cflag |= CS8;
        break;
    default:
        goto e;
    }

#ifndef CMSPAR
#define CMSPAR 0
#endif

    options.c_iflag &= ~(INPCK | ISTRIP);
    switch (parity) {
    case 'n':
    case 'N':
        options.c_cflag &= ~(PARENB | PARODD | CMSPAR);
        break;
    case 's':
    case 'S':
        options.c_cflag |= (PARENB | CMSPAR);
        options.c_cflag &= ~PARODD;
        break;
    case 'o':
    case 'O':
        options.c_cflag &= ~CMSPAR;
        options.c_cflag |= (PARENB | PARODD);
        break;
    case 'e':
    case 'E':
        options.c_cflag &= ~(PARODD | CMSPAR);
        options.c_cflag |= PARENB;
        break;
    default:
        goto e;
    }

    switch (stop) {
    case 1:
        options.c_cflag &= ~CSTOPB;
        break;
    case 2:
        options.c_cflag |= CSTOPB;
        break;
    default:
        goto e;
    }

    options.c_oflag &= ~(OPOST | ONLCR | OCRNL);
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ISIG | IEXTEN);
    options.c_iflag &= ~(INLCR | IGNCR | ICRNL | IGNBRK);

    options.c_cc[VTIME] = 0;
    options.c_cc[VMIN] = 1;

    tcflush(fd, TCIFLUSH);
    if (tcsetattr(fd, TCSANOW, &options) < 0) {
        goto e;
    }

    return fd;

e:
    close(fd);
    return -1;
}

void
uart_close(int fd) {
    close(fd);
}

int
uart_write(int fd, const char *data, size_t len) {
    size_t nleft;
    ssize_t nwritten;
    const char *ptr;

    ptr = data;
    nleft = len;

    while (nleft > 0) {
        if ((nwritten = write(fd, ptr, nleft)) <= 0) {
            if (nwritten < 0 && errno == EINTR)
                nwritten = 0;
            else
                return -1;
        }
        nleft -= nwritten;
        ptr += nwritten;
    }
    return len;
}

int
uart_read(int fd, char *data, size_t len) {
    size_t nleft;
    ssize_t nread;
    char *ptr;

    ptr = data;
    nleft = len;

    while (nleft > 0) {
        if ((nread = read(fd, ptr, nleft)) < 0) {
            if (errno == EINTR)
                nread = 0;
            else
                return -1;
        } else if (nread == 0)
            break;
        nleft -= nread;
        ptr += nread;
    }
    return len - nleft;
}

#endif /* UART_IMPLEMENTATION */