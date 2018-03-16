/*
 * pms5003st.h -- pms5003st utils, pm2.5 sensor by plantower.
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

#ifndef _PMS5003ST_H_
#define _PMS5003ST_H_

#ifdef __cplusplus
extern "C" {
#endif

/* generic includes. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <errno.h>
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define __bswap_16 OSSwapInt16
#else
#include <byteswap.h>
#endif

#if defined(__GNUC__) && (__GNUC__ >= 4)
#define PMS5003ST_API __attribute__((visibility("default")))
#else
#define PMS5003ST_API
#endif

struct pms5003st {
    int ver;
    int err;
    int pm1_0_std; /* PM1.0 concentration in standard material */
    int pm2_5_std; /* PM2.5 concentration in standard material */
    int pm10_std;  /* PM10 concentration in standard material */
    int pm1_0_atm; /* PM1.0 concentration in atmospheric environment */
    int pm2_5_atm; /* PM2.5 concentration in atmospheric environment */
    int pm10_atm;  /* PM10 concentration in atmospheric environment */
    int g_0_3um;
    int g_0_5um;
    int g_1_0um;
    int g_2_5um;
    int g_5_0um;
    int g_10um;
    float hcho;        /* mg/m3 */
    float temperature; /* C */
    float humidity;    /* % */
};

extern PMS5003ST_API int uart_open(const char *dev);

extern PMS5003ST_API void uart_close(int fd);

extern PMS5003ST_API int uart_write(int fd, const char *data, size_t len);

extern PMS5003ST_API int uart_can_read(int fd, int timeout);

extern PMS5003ST_API int uart_read(int fd, char *data, size_t len);

extern PMS5003ST_API int uart_set(int fd, int baude, int c_flow, int bits, char parity, int stop);

extern PMS5003ST_API int pms5003st_read(int fd, struct pms5003st *p);

extern PMS5003ST_API int pms5003st_json(struct pms5003st *p, char *str, size_t len);

extern PMS5003ST_API void pms5003st_print(struct pms5003st *p);

#ifdef __cplusplus
}
#endif

#endif /* _PMS5003ST_H_ */

#ifdef PMS5003ST_IMPLEMENTATION

int
uart_open(const char *dev) {
    int fd;

    fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0)
        return fd;
    if (fcntl(fd, F_SETFL, 0) < 0) {
        close(fd);
        return -1;
    }
    return fd;
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
        }
        else if (nread == 0)
            break;
        nleft -= nread;
        ptr += nread;
    }
    return len - nleft;
}

int
uart_can_read(int fd, int timeout) {
    int ret;
    fd_set rfds;
    struct timeval time;

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    time.tv_sec = timeout;
    time.tv_usec = 0;

    ret = select(fd + 1, &rfds, 0, 0, &time);
    return ret > 0;
}

int
uart_set(int fd, int baude, int c_flow, int bits, char parity, int stop) {
    struct termios options;

    /*获取终端属性*/
    if (tcgetattr(fd, &options) < 0) {
        return -1;
    }

    /*设置输入输出波特率，两者保持一致*/
    switch (baude) {
    case 4800:
        cfsetispeed(&options, B4800);
        cfsetospeed(&options, B4800);
        break;
    case 9600:
        cfsetispeed(&options, B9600);
        cfsetospeed(&options, B9600);
        break;
    case 19200:
        cfsetispeed(&options, B19200);
        cfsetospeed(&options, B19200);
        break;
    case 38400:
        cfsetispeed(&options, B38400);
        cfsetospeed(&options, B38400);
        break;
    default:
        return -1;
    }

    /*设置控制模式*/
    options.c_cflag |= CLOCAL; //保证程序不占用串口
    options.c_cflag |= CREAD;  //保证程序可以从串口中读取数据

    /*设置数据流控制*/
    switch (c_flow) {
    case 0: //不进行流控制
        options.c_cflag &= ~CRTSCTS;
        break;
    case 1: //进行硬件流控制
        options.c_cflag |= CRTSCTS;
        break;
    case 2: //进行软件流控制
        options.c_cflag |= IXON | IXOFF | IXANY;
        break;
    default:
        return -1;
    }

    /*设置数据位*/
    switch (bits) {
    case 5:
        options.c_cflag &= ~CSIZE; //屏蔽其它标志位
        options.c_cflag |= CS5;
        break;
    case 6:
        options.c_cflag &= ~CSIZE; //屏蔽其它标志位
        options.c_cflag |= CS6;
        break;
    case 7:
        options.c_cflag &= ~CSIZE; //屏蔽其它标志位
        options.c_cflag |= CS7;
        break;
    case 8:
        options.c_cflag &= ~CSIZE; //屏蔽其它标志位
        options.c_cflag |= CS8;
        break;
    default:
        return -1;
    }

    /*设置校验位*/
    switch (parity) {
    /*无奇偶校验位*/
    case 'n':
    case 'N':
        options.c_cflag &= ~PARENB; //PARENB：产生奇偶位，执行奇偶校验
        options.c_cflag &= ~INPCK;  //INPCK：使奇偶校验起作用
        break;
    /*设为空格,即停止位为2位*/
    case 's':
    case 'S':
        options.c_cflag &= ~PARENB; //PARENB：产生奇偶位，执行奇偶校验
        options.c_cflag &= ~CSTOPB; //CSTOPB：使用两位停止位
        break;
    /*设置奇校验*/
    case 'o':
    case 'O':
        options.c_cflag |= PARENB; //PARENB：产生奇偶位，执行奇偶校验
        options.c_cflag |= PARODD; //PARODD：若设置则为奇校验,否则为偶校验
        options.c_cflag |= INPCK;  //INPCK：使奇偶校验起作用
        options.c_cflag |= ISTRIP; //ISTRIP：若设置则有效输入数字被剥离7个字节，否则保留全部8位
        break;
    /*设置偶校验*/
    case 'e':
    case 'E':
        options.c_cflag |= PARENB;  //PARENB：产生奇偶位，执行奇偶校验
        options.c_cflag &= ~PARODD; //PARODD：若设置则为奇校验,否则为偶校验
        options.c_cflag |= INPCK;   //INPCK：使奇偶校验起作用
        options.c_cflag |= ISTRIP;  //ISTRIP：若设置则有效输入数字被剥离7个字节，否则保留全部8位
        break;
    default:
        return -1;
    }

    /*设置停止位*/
    switch (stop) {
    case 1:
        options.c_cflag &= ~CSTOPB; //CSTOPB：使用两位停止位
        break;
    case 2:
        options.c_cflag |= CSTOPB; //CSTOPB：使用两位停止位
        break;
    default:
        return -1;
    }

    /*设置输出模式为原始输出*/
    options.c_oflag &= ~OPOST; //OPOST：若设置则按定义的输出处理，否则所有c_oflag失效

    /*设置本地模式为原始模式*/
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    /*
     *ICANON：允许规范模式进行输入处理
     *ECHO：允许输入字符的本地回显
     *ECHOE：在接收EPASE时执行Backspace,Space,Backspace组合
     *ISIG：允许信号
     */

    /*设置等待时间和最小接受字符*/
    options.c_cc[VTIME] = 0; //可以在select中设置
    options.c_cc[VMIN] = 1;  //最少读取一个字符

    /*如果发生数据溢出，只接受数据，但是不进行读操作*/
    tcflush(fd, TCIFLUSH);

    /*激活配置*/
    if (tcsetattr(fd, TCSANOW, &options) < 0) {
        return -1;
    }

    return 0;
}

static int
_read(int fd, char *data, int len) {
    while (uart_can_read(fd, 15)) {
        return uart_read(fd, data, len);
    }
    return 0;
}

int
pms5003st_read(int fd, struct pms5003st *p) {
    char ch;
    size_t i;
    unsigned short len;
    unsigned short chk;
    unsigned short check_sum = 0;
    unsigned short data[17];

a:
    if (1 != _read(fd, &ch, 1))
        goto a;
    if (ch != 0x42)
        goto a;
    check_sum += ch;
    if (1 != _read(fd, &ch, 1))
        goto a;
    if (ch != 0x4d)
        goto a;
    check_sum += ch;
    if (2 != _read(fd, (char *)&len, 2))
        goto a;
    len = __bswap_16(len);
    if (len != 2 * 17 + 2)
        goto a;
    check_sum += (unsigned char)len;
    if (sizeof(data) != _read(fd, (char *)&data, sizeof(data)))
        goto a;
    for (i = 0; i < sizeof(data) / sizeof(short); i++)
        data[i] = __bswap_16(data[i]);
    for (i = 0; i < sizeof(data); i++)
        check_sum += ((unsigned char *)data)[i];
    if (2 != _read(fd, (char *)&chk, 2))
        goto a;
    chk = __bswap_16(chk);
    if (check_sum != chk) {
        fprintf(stderr, "[PMS5003ST] ERROR CHK:%d\n", chk);
        goto a;
    }

    p->ver = data[16] >> 8;
    p->err = data[16] & 0xff;
    p->pm1_0_atm = data[0];
    p->pm2_5_atm = data[1];
    p->pm10_atm = data[2];
    p->pm1_0_std = data[3];
    p->pm2_5_std = data[4];
    p->pm10_std = data[5];
    p->g_0_3um = data[6];
    p->g_0_5um = data[7];
    p->g_1_0um = data[8];
    p->g_2_5um = data[9];
    p->g_5_0um = data[10];
    p->g_10um = data[11];
    p->hcho = data[12] / 1000.0f;
    p->temperature = data[13] / 10.0f;
    p->humidity = data[14] / 10.0f;
    return 0;
}

int
pms5003st_json(struct pms5003st *p, char *str, size_t len) {
    return snprintf(str, len,
                    "{"
                    "\"ver\":%d,"
                    "\"err\":%d,"
                    "\"pm1_0_atm\":%d,"
                    "\"pm2_5_atm\":%d,"
                    "\"pm10_atm\":%d,"
                    "\"pm1_0_std\":%d,"
                    "\"pm2_5_std\":%d,"
                    "\"pm10_std\":%d,"
                    "\"g_0_3um\":%d,"
                    "\"g_0_5um\":%d,"
                    "\"g_1_0um\":%d,"
                    "\"g_2_5um\":%d,"
                    "\"g_5_0um\":%d,"
                    "\"g_10um\":%d,"
                    "\"hcho\":%.3f,"
                    "\"temperature\":%.1f,"
                    "\"humidity\":%.1f"
                    "}",
                    p->ver, p->err,
                    p->pm1_0_atm, p->pm2_5_atm, p->pm10_atm, p->pm1_0_std, p->pm2_5_std, p->pm10_std,
                    p->g_0_3um, p->g_0_5um, p->g_1_0um, p->g_2_5um, p->g_5_0um, p->g_10um,
                    p->hcho, p->temperature, p->humidity);
}

void
pms5003st_print(struct pms5003st *p) {
    printf(
        "PMS5003ST\n"
        "PM1.0(CF=1): %u\n"
        "PM2.5(CF=1): %u\n"
        "PM10 (CF=1): %u\n"
        "PM1.0 (STD): %u\n"
        "PM2.5 (STD): %u\n"
        "PM10  (STD): %u\n"
        ">0.3um     : %u\n"
        ">0.5um     : %u\n"
        ">1.0um     : %u\n"
        ">2.5um     : %u\n"
        ">5.0um     : %u\n"
        ">10um      : %u\n"
        "HCHO       : %.3f\n"
        "TEMPERATURE: %.1f\n"
        "HUMIDITY   : %.1f%%\n"
        "\n",
        p->pm1_0_atm, p->pm2_5_atm, p->pm10_atm, p->pm1_0_std, p->pm2_5_std, p->pm10_std,
        p->g_0_3um, p->g_0_5um, p->g_1_0um, p->g_2_5um, p->g_5_0um, p->g_10um,
        p->hcho, p->temperature, p->humidity);
}

#endif /* PMS5003ST_IMPLEMENTATION */
