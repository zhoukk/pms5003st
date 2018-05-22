#ifdef __linux__
#define HAVE_EPOLL 1
#endif

#if (defined(__APPLE__) && defined(MAC_OS_X_VERSION_10_6)) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined (__NetBSD__)
#define HAVE_KQUEUE 1
#endif

#ifdef __sun
#include <sys/feature_tests.h>
#ifdef _DTRACE_VERSION
#define HAVE_EVPORT 1
#endif
#endif

#define LIBMQTT_IMPLEMENTATION
#include "libmqtt.h"

#define LIBEVENT_IMPLEMENTATION
#include "libevent.h"

#define PMS5003ST_IMPLEMENTATION
#include "pms5003st.h"


#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>

struct pms5003st_mqtt {
    int ttyfd;
    int iofd;
    int timer_id;
    char devpath[1024];
    char host[1024];
    int port;
    struct libevent *evt;
    struct libmqtt *mqtt;
    int connected;
};

static struct pms5003st_mqtt *P;

static int __pms_connect();
static int __pms_retry();

static void
__close(void) {
    if (-1 != P->iofd) {
        libevent__del_file(P->evt, P->iofd, LIBEVENT_READABLE);
        close(P->iofd);
    }
    if (-1 != P->timer_id)
        libevent__del_time(P->evt, P->timer_id);
}

static void
__disconnect(void) {
    fprintf(stdout, "disconnected: %s\n", strerror(errno));
    __close();
    if (__pms_connect()) {
        if (-1 == libevent__set_time(P->evt, 1000, 0, __pms_retry)) {
            fprintf(stdout, "libevent__set_time: e: %d %s\n", errno, strerror(errno));
            return;
        }
    }
}


static void
__read(struct libevent *evt, void *ud, int fd, int mask) {
    int nread;
    char buff[4096];
    int rc;
    (void)evt;
    (void)ud;
    (void)mask;

    nread = read(fd, buff, sizeof(buff));
    if (nread == -1 && errno == EAGAIN) {
        return;
    }
    rc = LIBMQTT_SUCCESS;
    if (nread <= 0 || LIBMQTT_SUCCESS != (rc = libmqtt__parse(P->mqtt, buff, nread))) {
        if (rc != LIBMQTT_SUCCESS)
            fprintf(stderr, "libmqtt__parse: %s\n", libmqtt__strerror(rc));
        __disconnect();
    }
}

static int
__write(void *p, const char *data, int size) {
    (void)p;
    return size == write(P->iofd, data, size) ? 0 : -1;
}

static int
__update(struct libevent *evt, void *ud, int id) {
    int rc;
    (void)evt;
    (void)ud;
    (void)id;

    if (LIBMQTT_SUCCESS != (rc = libmqtt__update(P->mqtt))) {
        fprintf(stdout, "libmqtt__update: %s\n", libmqtt__strerror(rc));
        return -1;
    }
    return 1000;
}

static int
__set_non_block(int fd, int non_block) {
    int flags;

    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        return -1;
    }

    if (non_block)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) == -1) {
        return -1;
    }
    return 0;
}

static int
__set_tcp_nodelay(int fd, int nodelay) {
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) == -1) {
        return -1;
    }
    return 0;
}

static int
__set_tcp_keepalive(int fd, int keepalive) {
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) == -1) {
        return -1;
    }
    return 0;
}

static int
__set_tcp_reuseaddr(int fd, int reuse) {
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        return -1;
    }
    return 0;
}

static int
__tcp_connect(const char *addr, int port) {
    int s = -1, rv;
    char portstr[6];
    struct addrinfo hints, *servinfo, *p;

    snprintf(portstr, sizeof(portstr), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(addr, portstr, &hints, &servinfo)) != 0) {
        return -1;
    }
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
            continue;
        if (__set_tcp_reuseaddr(s, 1) == -1)
            goto err;
        if (connect(s, p->ai_addr, p->ai_addrlen) == -1) {
            close(s);
            s = -1;
            continue;
        }
        goto end;
    }

err:
    if (s != -1) {
        close(s);
        s = -1;
    }

end:
    freeaddrinfo(servinfo);
    return s;
}

static int
__connect(void) {
    int fd;
    int timer_id;

    fd = __tcp_connect(P->host, P->port);
    if (-1 == fd) {
        fprintf(stderr, "__tcp_connect: %s\n", strerror(errno));
        goto e1;
    }
    __set_non_block(fd, 1);
    __set_tcp_nodelay(fd, 1);
    __set_tcp_keepalive(fd, 1);

    if (-1 == libevent__set_file(P->evt, fd, LIBEVENT_READABLE, 0, __read)) {
        fprintf(stderr, "libevent__set_file: %s\n", strerror(errno));
        goto e2;
    }

    timer_id = libevent__set_time(P->evt, 1000, 0, __update);
    if (-1 == timer_id) {
        fprintf(stderr, "libevent__set_time: %s\n", strerror(errno));
        goto e3;
    }

    P->iofd = fd;
    P->timer_id = timer_id;
    return 0;

e3:
    libevent__del_file(P->evt, fd, LIBEVENT_READABLE);
e2:
    close(fd);
e1:
    return -1;
}

static int
__pms_update(struct libevent *evt, void *ud, int id) {
    int rc;
    struct pms5003st p;
    char str[1024] = {0};
    int len;
    (void)evt;
    (void)ud;
    (void)id;

    if (0 == P->connected) {
        return 100;
    }
    if (0 != pms5003st_read(P->ttyfd, &p)) {
        return 100;
    }
    len = pms5003st_json(&p, str, 1024);
    rc = libmqtt__publish(P->mqtt, 0, 0, 0, "pms5003st", str, len);
    if (rc != LIBMQTT_SUCCESS) {
        fprintf(stdout, "%s\n", libmqtt__strerror(rc));
    }

    len = sprintf(str, "pms5003,sensor=sensor1 ver=%d err=%d pm1_0_atm=%d pm2_5_atm=%d pm10_atm=%d"
            "pm1_0_std=%d pm2_5_std=%d pm10_std=%d g_0_3um=%d g_0_5um=%d g_1_0um=%d g_2_5um=%d g_5_0um=%d g_10um=%d"
            "hcho=%.3f temperature=%.1f humidity=%.1f", p.ver, p.err, p.pm1_0_atm, p.pm2_5_atm, p.pm10_atm,
            p.pm1_0_std, p.pm2_5_std, p.pm10_std, p.g_0_3um, p.g_0_5um, p.g_1_0um, p.g_2_5um, p.g_5_0um, p.g_10um,
            p.hcho, p.temperature, p.humidity);
    rc = libmqtt__publish(P->mqtt, 0, 0, 0, "pms5003st_influxdb", str, len);
    if (rc != LIBMQTT_SUCCESS) {
        fprintf(stdout, "%s\n", libmqtt__strerror(rc));
    }

    len = sprintf(str, "%.1f", p.temperature);
    rc = libmqtt__publish(P->mqtt, 0, 0, 0, "pms5003st_temperature", str, len);
    if (rc != LIBMQTT_SUCCESS) {
        fprintf(stdout, "%s\n", libmqtt__strerror(rc));
    }

    len = sprintf(str, "%.1f", p.humidity);
    rc = libmqtt__publish(P->mqtt, 0, 0, 0, "pms5003st_humidity", str, len);
    if (rc != LIBMQTT_SUCCESS) {
        fprintf(stdout, "%s\n", libmqtt__strerror(rc));
    }

    len = sprintf(str, "%d", p.pm2_5_std);
    rc = libmqtt__publish(P->mqtt, 0, 0, 0, "pms5003st_pm2_5", str, len);
    if (rc != LIBMQTT_SUCCESS) {
        fprintf(stdout, "%s\n", libmqtt__strerror(rc));
    }

    return 100;
}

static int
__pms_connect(void) {
    int rc;

    rc = __connect();
    if (rc) {
        fprintf(stdout, "__connect: %s\n", strerror(errno));
        return -1;
    }
    rc = libmqtt__connect(P->mqtt, 0, __write);
    if (rc != LIBMQTT_SUCCESS) {
        fprintf(stdout, "%s\n", libmqtt__strerror(rc));
        __close();
    }
    return 0;
}

static int
__pms_retry(struct libevent *evt, void *ud, int id) {
    (void)evt;
    (void)ud;
    (void)id;

    if (__pms_connect()) {
        return 1000;
    }
    return -1;
}

static void
__connack(struct libmqtt *mqtt, void *ud, int ack_flags, enum mqtt_crc return_code) {
    (void)ack_flags;
    (void)ud;

    if (return_code != MQTT_CRC_ACCEPTED) {
        fprintf(stdout, "%s\n", MQTT_CRC_NAMES[return_code]);
        return;
    }

    if (-1 == P->ttyfd) {
        P->ttyfd = uart_open(P->devpath);
        if (P->ttyfd < 0) {
            fprintf(stdout, "fatal: open(): %s: %s\n", P->devpath, strerror(errno));
            libmqtt__disconnect(mqtt);
            return;
        }
        uart_set(P->ttyfd, 9600, 0, 8, 'N', 1);
        P->connected = 1;
        if (-1 == libevent__set_time(P->evt, 100, 0, __pms_update)) {
            fprintf(stdout, "libevent__set_time: error\n");
            libmqtt__disconnect(mqtt);
            return;
        }
    }
}

static void
__puback(struct libmqtt *mqtt, void *ud, uint16_t id) {
    (void)mqtt;
    (void)ud;
    (void)id;
}

int
main(int argc, char *argv[]) {
    int rc;
    struct libmqtt *mqtt;
    struct libmqtt_cb cb = {
        .connack = __connack,
        .puback = __puback,
    };
    struct pms5003st_mqtt pm;

    if (argc < 3) {
        printf("usage: %s devpath host port\n", argv[0]);
        return 0;
    }

    memset(&pm, 0, sizeof pm);
    strcpy(pm.devpath, argv[1]);
    strcpy(pm.host, argv[2]);

    if (argc > 3) {
        pm.port = atoi(argv[3]);
    } else {
        pm.port = 1883;
    }
    pm.evt = libevent__create(128);
    pm.ttyfd = -1;

    P = &pm;

    rc = __connect();
    if (rc) {
        return 0;
    }

    rc = libmqtt__create(&mqtt, "pms5003st_pub", &pm, &cb);
    if (!rc) libmqtt__will(mqtt, 1, 2, "pms5003st_pub_state", "exit", 4);
    if (rc != LIBMQTT_SUCCESS) {
        fprintf(stdout, "%s\n", libmqtt__strerror(rc));
        return 0;
    }
    pm.mqtt = mqtt;

    if (__pms_connect()) {
        if (-1 == libevent__set_time(pm.evt, 1000, 0, __pms_retry)) {
            fprintf(stdout, "libevent__set_time: e: %d %s\n", errno, strerror(errno));
            return 0;
        }
    }

    libevent__loop(pm.evt);
    libevent__destroy(pm.evt);
    libmqtt__destroy(mqtt);
    if (pm.ttyfd != -1) {
        uart_close(pm.ttyfd);
    }
    return 0;
}
