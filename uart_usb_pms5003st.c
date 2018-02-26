#define PMS5003ST_IMPLEMENTATION
#include "pms5003st.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static void
set_interface_attribs(int fd, int speed) {
    struct termios tty;

    if (tcgetattr(fd, &tty) < 0) {
        fprintf(stderr, "fatal: tcgetattr(): %s\n", strerror(errno));
        exit(-1);
    }

    cfsetospeed(&tty, (speed_t)speed);
    cfsetispeed(&tty, (speed_t)speed);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "fatal: tcsetattr(): %s\n", strerror(errno));
        exit(-1);
    }
}

int main(int argc, char *argv[]) {
    int fd;

    if (argc < 2) {
        printf("usage: %s devpath\n", argv[0]);
        return 0;
    }

    fd = open(argv[1], O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        fprintf(stderr, "fatal: open(): %s: %s\n", argv[1], strerror(errno));
        exit(-1);
    }

    set_interface_attribs(fd, B9600);

    for (;;) {
        struct pms5003st p;

        sleep(1);
        if (0 != pms5003st_read(fd, &p)) {
            continue;
        }
        pms5003st_print(&p);
    }
    close(fd);
    return 0;
}
