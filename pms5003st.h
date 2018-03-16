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
#include <errno.h>
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define __bswap_16 OSSwapInt16
#else
#include <byteswap.h>
#endif

#if defined(__GNUC__) && (__GNUC__ >= 4)
# define PMS5003ST_API __attribute__((visibility("default")))
#else
# define PMS5003ST_API
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

extern PMS5003ST_API int pms5003st_read(int fd, struct pms5003st *p);

extern PMS5003ST_API int pms5003st_json(struct pms5003st *p, char *str, size_t len);

extern PMS5003ST_API void pms5003st_print(struct pms5003st *p);

#ifdef __cplusplus
}
#endif

#endif /* _PMS5003ST_H_ */


#ifdef PMS5003ST_IMPLEMENTATION

int
pms5003st_read(int fd, struct pms5003st *p) {
    char ch;
    size_t i;
    unsigned short len;
    unsigned short chk;
    unsigned short check_sum = 0;
    unsigned short data[17];

a:
    if (1 != read(fd, &ch, 1)) {
        goto a;
    }
    if (ch != 0x42) {
        goto a;
    }
    check_sum += ch;
    if (1 != read(fd, &ch, 1)) {
        goto a;
    }
    if (ch != 0x4d) {
        goto a;
    }
    check_sum += ch;
    if (2 != read(fd, &len, 2)) {
        goto a;
    }
    len = __bswap_16(len);
    if (len != 2 * 17 + 2) {
        goto a;
    }
    check_sum += (unsigned char)len;
    if (sizeof(data) != read(fd, &data, sizeof(data))) {
        goto a;
    }
    for (i = 0; i < sizeof(data) / sizeof(short); i++)
        data[i] = __bswap_16(data[i]);
    for (i = 0; i < sizeof(data); i++)
        check_sum += ((unsigned char *)data)[i];
    if (2 != read(fd, &chk, 2)) {
        goto a;
    }
    chk = __bswap_16(chk);
    if (check_sum != chk) {
        fprintf(stderr, "[PMS5003ST] ERROR CHK:%d\n", chk);
        return -1;
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
    return snprintf(str, len, "{"
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
        "}", p->ver, p->err,
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
           "\n", p->pm1_0_atm, p->pm2_5_atm, p->pm10_atm, p->pm1_0_std, p->pm2_5_std, p->pm10_std,
           p->g_0_3um, p->g_0_5um, p->g_1_0um, p->g_2_5um, p->g_5_0um, p->g_10um,
           p->hcho, p->temperature, p->humidity);
}

#endif /* PMS5003ST_IMPLEMENTATION */
