#include "ae_io.h"

#define PMS5003ST_IMPLEMENTATION
#include "pms5003st.h"

#include <termios.h>
#include <fcntl.h>

struct pms5003st_mqtt {
    int fd;
    char devpath[1024];
    struct libmqtt *mqtt;
    aeEventLoop *el;
};

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

static int
__update(aeEventLoop *el, long long id, void *privdata) {
    int rc;
    struct pms5003st_mqtt *pm;
    struct pms5003st p;
    char str[1024] = {0};
    int len;
    (void)el;
    (void)id;

    pm = (struct pms5003st_mqtt *)privdata;

    if (0 != pms5003st_read(pm->fd, &p)) {
        return 1000;
    }
    // pms5003st_print(&p);
    len = pms5003st_json(&p, str, 1024);
    printf("%s\n", str);
    rc = libmqtt__publish(pm->mqtt, 0, "libmqtt_pms5003st", 0, 0, str, len);
    if (rc != LIBMQTT_SUCCESS) {
        fprintf(stderr, "%s\n", libmqtt__strerror(rc));
    }

    return 1000;
}

static void
__connack(struct libmqtt *mqtt, void *ud, int ack_flags, enum mqtt_connack return_code) {
    struct pms5003st_mqtt *pm;
    (void)ack_flags;

    if (return_code != CONNACK_ACCEPTED) {
        fprintf(stderr, "%s\n", MQTT_CONNACK_NAMES[return_code]);
        return;
    }

    pm = (struct pms5003st_mqtt *)ud;
    pm->fd = open(pm->devpath, O_RDWR | O_NOCTTY | O_NDELAY);
    if (pm->fd < 0) {
        fprintf(stderr, "fatal: open(): %s: %s\n", pm->devpath, strerror(errno));
        libmqtt__disconnect(mqtt);
        return;
    }
    set_interface_attribs(pm->fd, B9600);

    if (AE_ERR == aeCreateTimeEvent(pm->el, 1000, __update, pm, 0)) {
        fprintf(stderr, "aeCreateTimeEvent: error\n");
        libmqtt__disconnect(mqtt);
        return;
    }
}

static void
__puback(struct libmqtt *mqtt, void *ud, uint16_t id) {
    (void)mqtt;
    (void)ud;
    printf("%u\n", id);
}

static void
__log(void *ud, const char *str) {
    (void)ud;
    fprintf(stdout, "%s\n", str);
}

static void
__disconnect(aeEventLoop *el, struct ae_io *io) {
    ae_io__close(el, io);
    aeStop(el);
}


int
main(int argc, char *argv[]) {
    int rc;
    struct libmqtt *mqtt;
    struct libmqtt_cb cb = {
        .connack = __connack,
        .puback = __puback,
    };
    aeEventLoop *el;
    struct ae_io *io;
    struct pms5003st_mqtt pm = {0, "", 0, 0};

    if (argc < 2) {
        printf("usage: %s devpath\n", argv[0]);
        return 0;
    }
    strcpy(pm.devpath, argv[1]);

    el = aeCreateEventLoop(128);

    rc = libmqtt__create(&mqtt, "libmqtt_pms5003st", &pm, &cb);
    if (!rc) libmqtt__debug(mqtt, __log);
    if (!rc) rc = libmqtt__will(mqtt, 1, 2, "libmqtt_pms5003st_state", "exit", 4);

    io = ae_io__connect(el, mqtt, "broker.hivemq.com", 1883, __disconnect);
    if (!io) {
        return 0;
    }

    if (!rc) rc = libmqtt__connect(mqtt, io, ae_io__write);
    if (rc != LIBMQTT_SUCCESS) {
        fprintf(stderr, "%s\n", libmqtt__strerror(rc));
        return 0;
    }

    pm.el = el;
    pm.mqtt = mqtt;

    aeMain(el);
    aeDeleteEventLoop(el);
    libmqtt__destroy(mqtt);
    return 0;
}
