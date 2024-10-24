#include "common.h"
#include "glib.h"
#include <librdkafka/rdkafka.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

Address central, kafka;
int id, listenPort;
rd_kafka_t *producer;
rd_kafka_t *consumer;
Response response;
Request request;

Coordinate pos;
Coordinate objective; // It's a pointer to allow NULL value
Coordinate coordBuffer;

void checkArguments(int argc, char *argv[]);
void authenticate();
void waitForService();
void go();
void nextStep();

void sendRequest() {
  sendEvent(producer, "requests", &request, sizeof(request));
}

int main(int argc, char *argv[]) {
  g_log_set_default_handler(log_handler, NULL);

  checkArguments(argc, argv);

  authenticate();
  request.id = id;

  producer = createKafkaAgent(&kafka, RD_KAFKA_PRODUCER);
  consumer = createKafkaAgent(&kafka, RD_KAFKA_CONSUMER);
  subscribeToTopics(&consumer, (const char *[]){"responses"}, 1);

  while (true) {
    waitForService();
    go();
    request.subject = CLIENT_PICKED_UP;
    sendRequest();
    objective = coordBuffer;
    go();
    request.subject = SERVICE_COMPLETED;
    sendRequest();
  }
}

void waitForService() {
  rd_kafka_message_t *msg = NULL;

  while (true) {
    if (msg != NULL)
      rd_kafka_message_destroy(msg);
    if (!(msg = poll_wrapper(consumer, 1000)))
      continue;

    memcpy(&response, msg->payload, sizeof(response));

    switch (response.subject) {
    case SERVICE_ACCEPTED:
      if (response.taxiId != id) {
        g_debug("Service accepted for taxi %d, but I'm taxi %d",
                response.taxiId, id);
        break;
      }

      memcpy(&objective, response.extraArgs, sizeof(Coordinate));
      memcpy(&coordBuffer, response.extraArgs + sizeof(Coordinate),
             sizeof(Coordinate));

      g_message("Service accepted: moving to [%i, %i] to pick up '%c'",
                objective.x + 1, objective.y + 1, response.clientId);
      return;

    default:
      g_debug("Unhandled subject: %i", response.subject);
      break;
    }
  }
}

void go() {
  while (pos.x != objective.x || pos.y != objective.y) {
    nextStep();
    g_message("Moving to [%i, %i]", pos.x + 1, pos.y + 1);
    request.subject = TAXI_MOVE;
    request.coord = pos;
    sendRequest();
    sleep(1);
  }
}

void checkArguments(int argc, char *argv[]) {
  char usage[100];
  sprintf(usage,
          "Usage: %s <central IP:port> <kafka IP:port> <listen port> <id>",
          argv[0]);

  if (argc < 3)
    g_error("%s", usage);

  if (sscanf(argv[1], "%[^:]:%d", central.ip, &central.port) != 2)
    g_error("Invalid central address. %s", usage);

  if (sscanf(argv[2], "%[^:]:%d", kafka.ip, &kafka.port) != 2)
    g_error("Invalid kafka address. %s", usage);

  if (sscanf(argv[3], "%d", &listenPort) != 1)
    g_error("Invalid listen port. %s", usage);

  if (sscanf(argv[4], "%d", &id) != 1)
    g_error("Invalid id. %s", usage);

  if (listenPort < 1 || listenPort > 65535)
    g_error("Invalid listen port, must be between 0 and 65535. %s", usage);

  if (central.port < 1 || central.port > 65535)
    g_error("Invalid central port, must be between 0 and 65535. %s", usage);

  if (kafka.port < 1 || kafka.port > 65535)
    g_error("Invalid kafka port, must be between 0 and 65535. %s", usage);

  if (id < 0 || id > 99)
    g_error("Invalid id, must be between 0 and 99. %s", usage);
}

void authenticate() {
  char buffer[BUFFER_SIZE] = {0};
  int sock = connectToServer(&central);

#define auth_error(format, ...)                                                \
  buffer[0] = EOT;                                                             \
  write(sock, buffer, BUFFER_SIZE);                                            \
  close(sock);                                                                 \
  g_error(format, ##__VA_ARGS__);

  if (sock == -1) {
    auth_error("Error connecting to central");
  }

  for (int i = 0; i < 5; i++) {
    buffer[0] = ENQ;
    g_debug("Sending ENQ");
    if (!write(sock, buffer, BUFFER_SIZE)) {
      auth_error("Error writing to central");
    }

    g_debug("Waiting for response");
    if (!read(sock, buffer, BUFFER_SIZE)) {
      auth_error("Error reading from central");
    }

    if (buffer[0] == ACK) {
      g_debug("Received ACK");
      break;
    }

    if (buffer[0] == NACK) {
      g_debug("Received NACK");
      if (i == 2) {
        auth_error("Connection refused. Try limit reached.");
      } else {
        g_warning("Connection refused. Retrying...");
      }
    } else {
      g_debug("Received unknown message: %i", buffer[1]);
      auth_error("Unknown message received.");
    }

    sleep(1);
  }

  for (int i = 0; i < 3; i++) {
    buffer[0] = STX;
    memcpy(buffer + 1, &id, sizeof(id));
    buffer[BUFFER_SIZE - 1] = ETX;

    g_debug("Sending STX");
    if (!write(sock, buffer, BUFFER_SIZE)) {
      auth_error("Error writing to central");
    }

    g_debug("Waiting for response");
    if (!read(sock, buffer, BUFFER_SIZE)) {
      auth_error("Error reading from central");
    }

    if (buffer[0] == ACK) {
      g_debug("Received ACK");
      g_message("Authentication successful. ID assigned: %i", id);
      break;
    }

    if (buffer[0] != NACK) {
      g_debug("Received unknown message: %i", buffer[1]);
      if (i == 2) {
        auth_error("Unknown message received. Try limit reached.");
      }
      g_warning("Unknown message received. Retrying...");
      continue;
    }

    int newId;

    memcpy(&newId, buffer + 1, sizeof(id));

    if (newId == -1) {
      if (i == 2) {
        auth_error("Couldn't assign a new ID. Try limit reached.");
      }
      g_warning("Couldn't assign a new ID. Retrying...");
      continue;
    }

    id = newId;
    g_message("Authentication successful. Requested ID couldn't be assigned, "
              "new ID: %i",
              id);
    break;
  }

  buffer[0] = EOT;
  write(sock, buffer, BUFFER_SIZE);
  close(sock);
}

void nextStep() {
  int possibleMoves[8][2] = {
      {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}, {0, 1},
  };

  Coordinate bestPos = {.x = -1, .y = -1};
  int bestDistance = INT_MAX;

  for (int i = 0; i < 8; i++) {
    Coordinate newPos = {.x = (pos.x + possibleMoves[i][0]) % 20,
                         .y = (pos.y + possibleMoves[i][1]) % 20};

    if (newPos.x < 0)
      newPos.x += 20;

    if (newPos.y < 0)
      newPos.y += 20;

    int distance = abs(newPos.x - objective.x) + abs(newPos.y - objective.y);

    if (distance < bestDistance) {
      bestDistance = distance;
      bestPos = newPos;
    }
  }

  pos = bestPos;
}