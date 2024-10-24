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
Coordinate pos;

void sendRequest() {
  sendEvent(producer, "requests", &request, sizeof(request));
}

void checkArguments(int argc, char *argv[]);

int main(int argc, char *argv[]) {
  rd_kafka_message_t *msg = NULL;

  g_log_set_default_handler(log_handler, NULL);
  checkArguments(argc, argv);

  producer = createKafkaAgent(&kafka, RD_KAFKA_PRODUCER);
  consumer = createKafkaAgent(&kafka, RD_KAFKA_CONSUMER);
  subscribeToTopics(&consumer, (const char *[]){"responses"}, 1);

  request.subject = NEW_CLIENT;
  request.id = id;
  request.coord = pos;
  sendRequest();

  g_message("Waiting for confirmation");
  while (true) {
    if (!(msg = poll_wrapper(consumer, 1000)))
      continue;

    memcpy(&response, msg->payload, sizeof(response));

    if (response.clientId != id ||
        (response.subject != CONFIRMATION && response.subject != ERROR)) {
      g_debug("Id received: %c", response.clientId);
      g_debug("Subject received: %i", response.subject);
      continue;
    }

    if (response.subject == ERROR)
      g_error("Central rejected the connection");

    g_message("Central accepted the connection");
    break;
  }

  request.subject = ASK_FOR_SERVICE;
  request.coord = pos;
  request.extraArgs[0] = 'F';

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
    case CONFIRMATION:
      g_message("Confirmation received");
      break;
    default:
      g_debug("Unhandled subject: %i", response.subject);
      break;
    }
  }
}

void checkArguments(int argc, char *argv[]) {
  char usage[100];

  sprintf(usage, "Usage: %s <Kafka IP:Port> <ID> <x> <y>", argv[0]);

  if (argc < 5) {
    g_error("%s", usage);
  }

  if (sscanf(argv[1], "%[^:]:%d", kafka.ip, &kafka.port) != 2)
    g_error("Invalid kafka address. %s", usage);

  if (sscanf(argv[2], "%c", &id) != 1)
    g_error("Invalid id. %s", usage);

  if (sscanf(argv[3], "%i", &pos.x) != 1)
    g_error("Invalid x. %s", usage);

  if (sscanf(argv[4], "%i", &pos.y) != 1)
    g_error("Invalid y. %s", usage);

  if (id < 'a' || id > 'z')
    g_error("Invalid id, must be between 'a' and 'z'. %s", usage);

  if (kafka.port < 1 || kafka.port > 65535)
    g_error("Invalid kafka port (%i), must be between 0 and 65535. %s",
            kafka.port, usage);
}