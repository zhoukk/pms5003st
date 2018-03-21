#define LIBMQTT_IMPLEMENTATION
#include "libmqtt.h"

#include "lib/ae.h"
#include "lib/anet.h"

#define PMS5003ST_IMPLEMENTATION
#include "pms5003st.h"

struct ae_io {
    int fd;
    long long timer_id;
    struct libmqtt *mqtt;
};

struct pms5003st_mqtt {
    int fd;
    char devpath[1024];
    char host[1024];
    int port;
    struct libmqtt *mqtt;
    aeEventLoop *el;
    int connected;
};

static struct pms5003st_mqtt P;

static int __pms_connect(aeEventLoop *el, struct libmqtt *mqtt);
static int __pms_retry(aeEventLoop *el, long long id, void *privdata);

static void
__close(aeEventLoop *el, struct ae_io *io) {
    if (AE_ERR != io->fd) {
        aeDeleteFileEvent(el, io->fd, AE_READABLE);
        close(io->fd);
    }
    if (AE_ERR != io->timer_id)
        aeDeleteTimeEvent(el, io->timer_id);
    free(io);
}

static void
__disconnect(aeEventLoop *el, struct ae_io *io) {
    struct libmqtt *mqtt;

    mqtt = io->mqtt;

    fprintf(stderr, "disconnected: %s\n", strerror(errno));
    __close(el, io);

    if (__pms_connect(el, mqtt)) {
        if (AE_ERR == aeCreateTimeEvent(el, 1000, __pms_retry, mqtt, 0)) {
            fprintf(stderr, "aeCreateTimeEvent: error\n");
            return;
        }
    }
}

static void
__read(aeEventLoop *el, int fd, void *privdata, int mask) {
    struct ae_io *io;
    int nread;
    char buff[4096];
    int rc;
    (void)mask;

    io = (struct ae_io *)privdata;
    nread = read(fd, buff, sizeof(buff));
    if (nread == -1 && errno == EAGAIN) {
        return;
    }
    rc = LIBMQTT_SUCCESS;
    if (nread <= 0 || LIBMQTT_SUCCESS != (rc = libmqtt__read(io->mqtt, buff, nread))) {
        if (rc != LIBMQTT_SUCCESS)
            fprintf(stderr, "libmqtt__read: %s\n", libmqtt__strerror(rc));
        __disconnect(el, io);
    }
}

static int
__write(void *p, const char *data, int size) {
    struct ae_io *io;

    io = (struct ae_io *)p;
    return write(io->fd, data, size);
}

static int
__update(aeEventLoop *el, long long id, void *privdata) {
    struct ae_io *io;
    int rc;
    (void)el;
    (void)id;

    io = (struct ae_io *)privdata;
    if (LIBMQTT_SUCCESS != (rc = libmqtt__update(io->mqtt))) {
        fprintf(stderr, "libmqtt__update: %s\n", libmqtt__strerror(rc));
        return AE_NOMORE;
    }
    return 1000;
}


static struct ae_io *
__connect(aeEventLoop *el, char *host, int port) {
    struct ae_io *io;
    int fd;
    long long timer_id;
    char err[ANET_ERR_LEN];

    fd = anetTcpConnect(err, host, port);
    if (ANET_ERR == fd) {
        fprintf(stderr, "anetTcpConnect: %s\n", err);
        goto e1;
    }
    anetNonBlock(0, fd);
    anetEnableTcpNoDelay(0, fd);
    anetTcpKeepAlive(0, fd);

    io = (struct ae_io *)malloc(sizeof *io);
    memset(io, 0, sizeof *io);

    if (AE_ERR == aeCreateFileEvent(el, fd, AE_READABLE, __read, io)) {
        fprintf(stderr, "aeCreateFileEvent: error\n");
        goto e2;
    }

    timer_id = aeCreateTimeEvent(el, 1000, __update, io, 0);
    if (AE_ERR == timer_id) {
        fprintf(stderr, "aeCreateTimeEvent: error\n");
        goto e3;
    }

    io->fd = fd;
    io->timer_id = timer_id;
    return io;

e3:
    aeDeleteFileEvent(el, fd, AE_READABLE);
e2:
    close(fd);
e1:
    return 0;
}

static int
__pms_update(aeEventLoop *el, long long id, void *privdata) {
    int rc;
    struct pms5003st_mqtt *pm;
    struct pms5003st p;
    char str[1024] = {0};
    int len;
    (void)el;
    (void)id;

    pm = (struct pms5003st_mqtt *)privdata;

    if (0 == pm->connected) {
        return 100;
    }
    if (0 != pms5003st_read(pm->fd, &p)) {
        return 100;
    }
    len = pms5003st_json(&p, str, 1024);
    rc = libmqtt__publish(pm->mqtt, 0, "libmqtt_pms5003st", 0, 0, str, len);
    if (rc != LIBMQTT_SUCCESS) {
        fprintf(stderr, "%s\n", libmqtt__strerror(rc));
    }

    return 100;
}

static int
__pms_connect(aeEventLoop *el, struct libmqtt *mqtt) {
    struct ae_io *io;
    int rc;

    io = __connect(el, P.host, P.port);
    if (!io) {
        fprintf(stderr, "ae_io__connect: %s\n", strerror(errno));
        return -1;
    }
    io->mqtt = mqtt;
    rc = libmqtt__connect(mqtt, io, __write);
    if (rc != LIBMQTT_SUCCESS) {
        fprintf(stderr, "%s\n", libmqtt__strerror(rc));
        __close(el, io);
    }
    return 0;
}

static int
__pms_retry(aeEventLoop *el, long long id, void *privdata) {
    struct libmqtt *mqtt;
    (void)id;

    mqtt = (struct libmqtt *)privdata;

    if (__pms_connect(el, mqtt)) {
        return 1000;
    }

    return AE_NOMORE;
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
    if (-1 == pm->fd) {
        pm->fd = uart_open(pm->devpath);
        if (pm->fd < 0) {
            fprintf(stderr, "fatal: open(): %s: %s\n", pm->devpath, strerror(errno));
            libmqtt__disconnect(mqtt);
            return;
        }
        uart_set(pm->fd, 9600, 0, 8, 'N', 1);
        pm->connected = 1;
        if (AE_ERR == aeCreateTimeEvent(pm->el, 100, __pms_update, pm, 0)) {
            fprintf(stderr, "aeCreateTimeEvent: error\n");
            libmqtt__disconnect(mqtt);
            return;
        }
    }
}

static void
__puback(struct libmqtt *mqtt, void *ud, uint16_t id) {
    (void)mqtt;
    (void)ud;
    printf("%u\n", id);
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
    struct pms5003st_mqtt pm = {-1, "", "", 1883, 0, 0, 0};

    if (argc < 3) {
        printf("usage: %s devpath host port\n", argv[0]);
        return 0;
    }
    strcpy(pm.devpath, argv[1]);
    strcpy(pm.host, argv[2]);

    if (argc > 3) {
        pm.port = atoi(argv[3]);
    }

    el = aeCreateEventLoop(128);
    pm.el = el;

    rc = libmqtt__create(&mqtt, "libmqtt_pms5003st", &pm, &cb);
    if (!rc) libmqtt__will(mqtt, 1, 2, "libmqtt_pms5003st_state", "exit", 4);
    if (rc != LIBMQTT_SUCCESS) {
        fprintf(stderr, "%s\n", libmqtt__strerror(rc));
        return 0;
    }
    pm.mqtt = mqtt;

    memcpy(&P, &pm, sizeof pm);

    if (__pms_connect(el, mqtt)) {
        if (AE_ERR == aeCreateTimeEvent(el, 1000, __pms_retry, mqtt, 0)) {
            fprintf(stderr, "aeCreateTimeEvent: error\n");
            return 0;
        }
    }

    aeMain(el);
    aeDeleteEventLoop(el);
    libmqtt__destroy(mqtt);
    if (pm.fd != -1) {
        uart_close(pm.fd);
    }
    return 0;
}
