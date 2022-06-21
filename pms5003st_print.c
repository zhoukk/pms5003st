#define PMS5003ST_IMPLEMENTATION
#include "pms5003st.h"

#define UART_IMPLEMENTATION
#include "uart.h"

int
main(int argc, char *argv[]) {
    int fd;

    if (argc < 2) {
        printf("usage: %s devpath\n", argv[0]);
        return 0;
    }

    fd = uart_open(argv[1], 9600, 0, 8, 'N', 1);
    if (fd < 0) {
        fprintf(stderr, "fatal: uart_open(): %s: %s\n", argv[1], strerror(errno));
        exit(-1);
    }

    for (;;) {
        struct pms5003st p;

        if (0 != pms5003st_read(fd, uart_read, &p)) {
            continue;
        }
        pms5003st_print(&p);
    }

    uart_close(fd);
    return 0;
}
