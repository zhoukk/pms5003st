#define MQTT_CLI_LINUX_PLATFORM
#define MQTT_CLI_IMPL
#include "mqtt_cli.h"

#define PMS5003ST_IMPLEMENTATION
#include "pms5003st.h"

#include <pthread.h>

struct pms5003st_runtime_arg {
  const char *devpath;
  mqtt_cli_t *m;
};

static void _connack(mqtt_cli_t *m, void *ud, const mqtt_packet_t *pkt) {
  (void)m;
  (void)ud;

  if (pkt->ver == MQTT_VERSION_3) {
    if (pkt->v.connack.v3.return_code != MQTT_CRC_ACCEPTED) {
      printf("Connack, %s\n", mqtt_crc_name(pkt->v.connack.v3.return_code));
      return;
    }
  } else if (pkt->ver == MQTT_VERSION_4) {
    if (pkt->v.connack.v4.return_code != MQTT_CRC_ACCEPTED) {
      printf("Connack, %s\n", mqtt_crc_name(pkt->v.connack.v4.return_code));
      return;
    }
  }
}

static void *pms5330st_runtime(void *arg) {
  struct pms5003st_runtime_arg *rarg = (struct pms5003st_runtime_arg *)arg;
  while (1) {
    int uart_fd = uart_open(rarg->devpath);
    if (uart_fd < 0) {
      fprintf(stderr, "uart_open(): %s: %s\n", rarg->devpath, strerror(errno));
      sleep(1);
      continue;
    }
    if (0 != uart_set(uart_fd, 9600, 0, 8, 'N', 1)) {
      fprintf(stderr, "uart_set(): %s: %s\n", rarg->devpath, strerror(errno));
      sleep(1);
      continue;
    }

    while (1) {
      struct pms5003st p;
      mqtt_str_t message;
      char str[1024] = {0};

      if (0 != pms5003st_read(uart_fd, &p)) {
        fprintf(stderr, "pms5003st_read(): %s\n", strerror(errno));
        break;
      }
      pms5003st_print(&p);
      message.n = pms5003st_json(&p, str, 1024);
      message.s = str;
      mqtt_cli_publish(rarg->m, 0, "pms5003st", MQTT_QOS_0, &message, 0);
    }
    uart_close(uart_fd);
    sleep(1);
  }
}

int main(int argc, char *argv[]) {
  mqtt_cli_t *m;
  void *net;

  if (argc < 2) {
    printf("usage: %s host dev\n", argv[0]);
    return EXIT_FAILURE;
  }

  mqtt_cli_conf_t config = {
      .client_id = "pms5003st_pub",
      .version = MQTT_VERSION_4,
      .keep_alive = 60,
      .clean_session = 1,
      .auth =
          {
              .username = "pms5003st_pub",
              .password = "pub",
          },
      .lwt =
          {
              .retain = 1,
              .topic = "pms5003st_pub_state",
              .qos = MQTT_QOS_1,
              .message = {.s = "exit", .n = 4},
          },
      .cb =
          {
              .connack = _connack,
          },
      .ud = 0,
  };

  net = linux_tcp_connect(argv[1], MQTT_TCP_PORT);
  if (!net) {
    fprintf(stderr, "mqtt broker connect error\n");
    return EXIT_FAILURE;
  }

  m = mqtt_cli_create(&config);
  mqtt_cli_connect(m);

  struct pms5003st_runtime_arg arg = {
      .devpath = argv[2],
      .m = m,
  };

  pthread_t tid;
  if (pthread_create(&tid, 0, pms5330st_runtime, &arg)) {
    fprintf(stderr, "fatal: pthread_create(): %s\n", strerror(errno));
    return EXIT_FAILURE;
  }

  while (1) {
    mqtt_str_t outgoing, incoming;
    uint64_t t1, t2;

    t1 = linux_time_now();
    mqtt_cli_outgoing(m, &outgoing);
    if (linux_tcp_transfer(net, &outgoing, &incoming)) {
      break;
    }
    if (mqtt_cli_incoming(m, &incoming)) {
      break;
    }
    t2 = linux_time_now();
    if (mqtt_cli_elapsed(m, t2 - t1)) {
      break;
    }
  }

  mqtt_cli_destroy(m);
  linux_tcp_close(net);
  return 0;
}
