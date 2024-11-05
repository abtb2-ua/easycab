#include "common.h"
#include "glib.h"
#include <bits/pthreadtypes.h>
#include <librdkafka/rdkafka.h>
#include <pthread.h>

// Connection parameters
const int COURTESY_TIME = 1500;
Address central, kafka;
int listenPort;

// Connection related variables
Response response;
int id;

// Motion parameters
bool stopped = true;  // Doesn't include when stopped because of sensor
bool canMove = false; // Whether it's possible to move (e.g. sensor connected)
bool stop = false;    // When the program is supposed to stop
Coordinate pos = {.x = 0, .y = 0};
Coordinate objective = {.x = 0, .y = 0};
IMPORTANCE importance;
int reason;

// Thread/process flow control
pthread_mutex_t mut, pos_mut;
pthread_cond_t cond, socket_bound_cond;

void checkArguments(int argc, char *argv[]);
void *connectToSensor();
bool communicateWithSensor(int sensorSocket, rd_kafka_t *producer,
                           Request *request);
void *connectToCentral();
void *run();
bool getGlobal(bool *global);
void nextStep();
void sendRequest(rd_kafka_t *producer, Request *request);
void authenticate();
void *ping();

int main(int argc, char *argv[]) {
  pthread_t thread_sensor;
  pthread_t thread_central;
  pthread_t thread_run;
  pthread_t ping_thread;

  g_log_set_default_handler(log_handler, NULL);

  pthread_create(&thread_sensor, NULL, connectToSensor, NULL);

  checkArguments(argc, argv);

  pthread_mutex_init(&mut, NULL);
  pthread_mutex_init(&pos_mut, NULL);
  pthread_cond_init(&cond, NULL);
  pthread_cond_init(&socket_bound_cond, NULL);

  authenticate();

  pthread_create(&thread_central, NULL, connectToCentral, NULL);
  pthread_create(&thread_run, NULL, run, NULL);
  pthread_create(&ping_thread, NULL, ping, NULL);

  pthread_join(thread_central, NULL);
  pthread_join(thread_run, NULL);
  pthread_join(thread_sensor, NULL);
  pthread_join(ping_thread, NULL);

  pthread_mutex_destroy(&mut);
  pthread_mutex_destroy(&pos_mut);
  pthread_cond_destroy(&cond);
  pthread_cond_destroy(&socket_bound_cond);

  return 0;
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

bool getGlobal(bool *global) {
  pthread_mutex_lock(&mut);
  int b = *global;
  pthread_mutex_unlock(&mut);
  return b;
}

void *connectToCentral() {
  char kafkaId[50];
  sprintf(kafkaId, "taxi-%d-consumer", id);
  rd_kafka_t *consumer = createKafkaAgent(&kafka, RD_KAFKA_CONSUMER, kafkaId);
  sprintf(kafkaId, "taxi-%d-central-producer", id);
  rd_kafka_t *producer = createKafkaAgent(&kafka, RD_KAFKA_PRODUCER, kafkaId);
  rd_kafka_message_t *msg = NULL;
  subscribeToTopics(&consumer, (const char *[]){"taxi_responses"}, 1);
  Request request;
  request.id = id;

  while (!getGlobal(&stop)) {
    if (msg != NULL)
      rd_kafka_message_destroy(msg);
    if (!(msg = poll_wrapper(consumer, 1000)))
      continue;

    memcpy(&response, msg->payload, sizeof(response));

    if (response.id != id)
      continue;

    switch (response.subject) {
    case TRESPONSE_GOTO:
      pthread_mutex_lock(&mut);
      stopped = false;
      if (!canMove) {
        request.subject = REQUEST_TAXI_CANT_MOVE_REMINDER;
        sendRequest(producer, &request);
      }
      memcpy(&objective, response.data, sizeof(Coordinate));
      pthread_mutex_unlock(&mut);
      g_message("Central ordered to move to [%i, %i]", objective.x + 1,
                objective.y + 1);
      sleep(1); // Give time to central to process the request
      break;
    case TRESPONSE_STOP:
      pthread_mutex_lock(&mut);
      stopped = true;
      pthread_mutex_unlock(&mut);
      g_message("Central ordered to stop");
      break;
    case TRESPONSE_CONTINUE:
      pthread_mutex_lock(&mut);
      stopped = false;
      if (!canMove) {
        request.subject = REQUEST_TAXI_CANT_MOVE_REMINDER;
        sendRequest(producer, &request);
      }
      pthread_mutex_unlock(&mut);
      g_message("Central ordered to continue");
      break;
    case TRESPONSE_CHANGE_POSITION:
      pthread_mutex_lock(&pos_mut);
      memcpy(&pos, response.data, sizeof(Coordinate));
      pthread_mutex_unlock(&pos_mut);
      g_message("Central ordered to change position to [%i, %i]", pos.x + 1,
                pos.y + 1);
      break;
    default:
      continue;
    }
  }

  rd_kafka_consumer_poll(consumer, 100);
  rd_kafka_consumer_close(consumer);
  rd_kafka_destroy(consumer);

  g_debug("Central exiting...");
  return NULL;
}

void *connectToSensor() {
  int server = openSocket(listenPort);
  int sensorSocket;
  char buffer[BUFFER_SIZE];
  char kafkaId[50];
  sprintf(kafkaId, "taxi-%d-sensor-producer", id);
  rd_kafka_t *producer = createKafkaAgent(&kafka, RD_KAFKA_PRODUCER, kafkaId);
  Request request;
  request.id = id;

  pthread_cond_signal(&socket_bound_cond);

  while (true) {
    g_message("Waiting connection from sensor...");
    sensorSocket = accept(server, NULL, NULL);

    if (sensorSocket == -1) {
      g_warning("Error accepting connection");
      continue;
    }

    if (read(sensorSocket, buffer, BUFFER_SIZE) <= 0) {
      g_warning("Error reading from socket");
      continue;
    }

    if (buffer[0] != ENQ) {
      g_warning("Invalid message received: %i", buffer[0]);
      continue;
    }

    buffer[0] = ACK;
    write(sensorSocket, buffer, BUFFER_SIZE);
    g_message("Sensor connected");

    if (!communicateWithSensor(sensorSocket, producer, &request)) {
      close(sensorSocket);
      close(server);
      g_debug("Sensor exiting...");
      return NULL;
    }

    pthread_mutex_lock(&mut);
    if (canMove) {
      canMove = false;
      request.subject = REQUEST_TAXI_CANT_MOVE;
      sendRequest(producer, &request);
    }
    pthread_mutex_unlock(&mut);
    g_warning("Sensor disconnected");
  }

  return NULL;
}

bool communicateWithSensor(int sensorSocket, rd_kafka_t *producer,
                           Request *request) {
  fd_set readfds;
  char buffer[BUFFER_SIZE];
  bool printedMsg = false;

  while (true) {
    FD_ZERO(&readfds);
    FD_SET(sensorSocket, &readfds);
    if (select(sensorSocket + 1, &readfds, NULL, NULL,
               &(struct timeval){0, COURTESY_TIME * 1000}) <= 0) {
      // FATAL("Sensor has disconnected");
      g_warning("Error reading from socket");
      break;
    }

    if (read(sensorSocket, buffer, BUFFER_SIZE) <= 0) {
      // FATAL("Error reading from socket");
      g_warning("Error reading from socket");
      break;
    }

    if (buffer[0] != STX) {
      g_warning("Invalid message received");
      break;
    }

    pthread_mutex_lock(&mut);
    if (buffer[1] != canMove) {
      canMove = buffer[1];
      request->subject =
          canMove ? REQUEST_TAXI_CAN_MOVE : REQUEST_TAXI_CANT_MOVE;
      sendRequest(producer, request);
    }
    memcpy(&importance, buffer + 3, sizeof(IMPORTANCE));
    memcpy(&reason, buffer + 3 + sizeof(IMPORTANCE), sizeof(int));

    if (buffer[2]) {
      request->subject = REQUEST_TAXI_FATAL_ERROR;
      sendRequest(producer, request);

      g_critical("Error couldn't be resolved. Terminating...");
      stop = true;
      pthread_mutex_unlock(&mut);
      sleep(2);
      g_debug("Sleeped");
      pthread_mutex_lock(&mut);
      pthread_cond_signal(&cond);
      pthread_mutex_unlock(&mut);
      return false;
    }

    buffer[0] = STX;
    buffer[1] = stopped;
    write(sensorSocket, buffer, BUFFER_SIZE);
    pthread_mutex_unlock(&mut);

    if (canMove && !getGlobal(&stopped)) {
      pthread_cond_signal(&cond);
      printedMsg = false;
    } else if (!canMove && !printedMsg) {
      g_warning("Cannot move: %s", inconveniences[importance][reason]);
      printedMsg = true;
    }
  }
  return true;
}

void *run() {
  char kafkaId[50];
  sprintf(kafkaId, "taxi-%d-run-producer", id);
  rd_kafka_t *producer = createKafkaAgent(&kafka, RD_KAFKA_PRODUCER, kafkaId);
  Request request;
  request.id = id;

  while (!getGlobal(&stop)) {
    pthread_mutex_lock(&mut);
    pthread_cond_wait(&cond, &mut);
    pthread_mutex_unlock(&mut);

    if (getGlobal(&stop))
      break;

    pthread_mutex_lock(&pos_mut);
    nextStep();
    g_message("Moving to [%i, %i]", pos.x + 1, pos.y + 1);
    request.subject = REQUEST_TAXI_MOVE;
    request.coord = pos;
    sendRequest(producer, &request);

    bool arrived = pos.x == objective.x && pos.y == objective.y;
    pthread_mutex_unlock(&pos_mut);

    if (arrived) {
      g_message("Destination reached. Stopping...");
      pthread_mutex_lock(&mut);
      stopped = true;
      pthread_mutex_unlock(&mut);

      request.subject = REQUEST_DESTINATION_REACHED;
      sendRequest(producer, &request);
    }
  }

  g_debug("Run exiting...");
  return NULL;
}

int min(int a, int b) { return a < b ? a : b; }

int sphericalDistance(Coordinate *a, Coordinate *b) {
  return min(abs(a->x - b->x), 20 - abs(a->x - b->x)) +
         min(abs(a->y - b->y), 20 - abs(a->y - b->y));
}

void nextStep() {
  if (pos.x == objective.x && pos.y == objective.y)
    return;

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

    int distance = sphericalDistance(&newPos, &objective);

    if (distance < bestDistance) {
      bestDistance = distance;
      bestPos = newPos;
    }
  }

  pos = bestPos;
}

void sendRequest(rd_kafka_t *producer, Request *request) {
  sendEvent(producer, "requests", request, sizeof(Request));
}

void authenticate() {
  pthread_mutex_lock(&mut);
  pthread_cond_wait(&socket_bound_cond, &mut);
  pthread_mutex_unlock(&mut);

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

    auth_error("Authentication failed. Id %i is already in use", id);
    break;
  }

  buffer[0] = EOT;
  write(sock, buffer, BUFFER_SIZE);
  close(sock);
}

void *ping() {
  rd_kafka_t *localProducer =
      createKafkaAgent(&kafka, RD_KAFKA_PRODUCER, "customer-ping-producer");
  Request request;
  request.subject = PING_TAXI;
  request.id = id;

  while (!getGlobal(&stop)) {
    sendEvent(localProducer, "requests", &request, sizeof(Request));
    usleep(PING_CADENCE * 1000 * 1000);
  }

  g_debug("Ping exiting...");
  return NULL;
}