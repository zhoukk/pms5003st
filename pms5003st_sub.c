#define MQTT_CLI_LINUX_PLATFORM
#define MQTT_CLI_IMPL
#include "mqtt_cli.h"

static void _publish(mqtt_cli_t *m, void *ud, const mqtt_packet_t *pkt) {
  (void)m;
  (void)ud;

  printf("[%.*s] %.*s\n", MQTT_STR_PRINT(pkt->v.publish.topic_name),
         MQTT_STR_PRINT(pkt->p.publish.message));
}

static void _suback(mqtt_cli_t *m, void *ud, const mqtt_packet_t *pkt) {
  (void)m;
  (void)ud;
  (void)pkt;

  printf("Subscribed\n");
}

static void _unsuback(mqtt_cli_t *m, void *ud, const mqtt_packet_t *pkt) {
  (void)ud;
  (void)pkt;

  mqtt_cli_disconnect(m);
}

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

  const char *topic = "pms5003st";
  mqtt_qos_t qos = MQTT_QOS_0;

  mqtt_cli_subscribe(m, 1, (const char **)&topic, &qos, 0);
}

int main(int argc, char *argv[]) {
  if (argc < 1) {
    printf("usage: %s host\n", argv[0]);
    return EXIT_FAILURE;
  }

  mqtt_cli_conf_t config = {
      .client_id = "pms5003st_sub",
      .version = MQTT_VERSION_4,
      .keep_alive = 60,
      .clean_session = 1,
      .auth =
          {
              .username = "pms5003st_sub",
              .password = "sub",
          },
      .lwt =
          {
              .retain = 1,
              .topic = "pms5003st_sub_state",
              .qos = MQTT_QOS_1,
              .message = {.s = "exit", .n = 4},
          },
      .cb =
          {
              .connack = _connack,
              .suback = _suback,
              .unsuback = _unsuback,
              .publish = _publish,
          },
      .ud = 0,
  };

  mqtt_cli_t *m = mqtt_cli_create(&config);

  while (1) {
    void *net = linux_tcp_connect(argv[1], MQTT_TCP_PORT);
    if (!net) {
      fprintf(stderr, "linux_tcp_connect(): %s\n", strerror(errno));
      sleep(1);
      continue;
    }
    mqtt_cli_connect(m);

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
    linux_tcp_close(net);
  }

  mqtt_cli_destroy(m);

  return 0;
}
