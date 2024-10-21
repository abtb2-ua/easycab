#include "common.h"
#include "glib.h"
#include <librdkafka/rdkafka.h>
#include <stdio.h>
#include <time.h>

Address kafka;
rd_kafka_t *producer;
rd_kafka_t *consumer;
char id;
Request request;
Response response;
Coordinate pos = {.x = 8, .y = 12};

void checkArguments(int argc, char *argv[]);

int main(int argc, char *argv[]) {
  rd_kafka_message_t *msg = NULL;

  g_log_set_default_handler(log_handler, NULL);
  checkArguments(argc, argv);

  producer = createKafkaAgent(&kafka, RD_KAFKA_PRODUCER);
  consumer = createKafkaAgent(&kafka, RD_KAFKA_CONSUMER);
  subscribeToTopics(&consumer, (const char *[]){"responses"}, 1);

  request.subject = ASK_FOR_SERVICE;
  request.id = id;
  request.coord = pos;
  char obj = 'H';
  request.extraArgs[0] = obj;

  sendEvent(producer, "requests", &request, sizeof(request));

  while (true) {
    if (msg != NULL)
      rd_kafka_message_destroy(msg);
    if (!(msg = poll_wrapper(consumer, 1000)))
      continue;

    memcpy(&response, msg->payload, sizeof(response));

    switch (response.subject) {
    case SERVICE_ACCEPTED:
      g_message("Service accepted");
      break;
    case SERVICE_DENIED:
      g_message("Service denied");
      break;
    default:
      g_debug("Unhandled subject: %i", response.subject);
      break;
    }
  }
}

void checkArguments(int argc, char *argv[]) {
  char usage[100];

  sprintf(usage, "Usage: %s <Kafka IP:Port> <ID>", argv[0]);

  if (argc < 3) {
    g_error("%s", usage);
  }

  if (sscanf(argv[1], "%[^:]:%d", kafka.ip, &kafka.port) != 2)
    g_error("Invalid kafka address. %s", usage);

  if (sscanf(argv[2], "%c", &id) != 1)
    g_error("Invalid id. %s", usage);

  if (id < 'a' || id > 'z')
    g_error("Invalid id, must be between 'a' and 'z'. %s", usage);

  if (kafka.port < 1 || kafka.port > 65535)
    g_error("Invalid kafka port (%i), must be between 0 and 65535. %s",
            kafka.port, usage);
}