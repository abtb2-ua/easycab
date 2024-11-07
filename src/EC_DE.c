#include "EC_DE_ncurses_gui.h"
#include "common.h"
#include "glib.h"
#include <bits/pthreadtypes.h>
#include <librdkafka/rdkafka.h>
#include <pthread.h>
#include <string.h>

// Connection parameters
const int COURTESY_TIME = 1500;
Address central, kafka;
int listenPort;

// Connection related variables
Response response;
char session[UUID_LENGTH];
int id;

// Connection with the GUI
int gui_pipe[2];

// Taxi parameters
bool orderedToStop = true; // Doesn't include when stopped because of sensor
bool canMove = false;      // Whether it's possible to move (e.g. sensor connected)
bool stopProgram = false;  // When the program is supposed to stop
bool sensorConnected = false;
Coordinate pos = {.x = 0, .y = 0};       // Where's the taxi
Coordinate objective = {.x = 0, .y = 0}; // Where the taxi is going towards
IMPORTANCE importance;                   // Importance of the inconvenience detected by the sensor
SUBJECT lastOrder = -1;    // Last order sent from the central. START_SERVICE is considered a GOTO
Coordinate lastOrderCoord; // Coordinate of the last order (if it's a GOTO or a CHANGE_POSITION)
char service = -1;         // Client that is currently serving the taxi
bool lastOrderCompleted = false; // Whether the last order has been completed or not
int reason; // Reason of the inconvenience detected by the sensor. It's an index for the
            // inconveniences array in common.h/common.c

// Thread/process flow control
pthread_mutex_t mut, pos_mut;
pthread_cond_t cond, socket_bound_cond, authenticated_cond;

/// @brief Parses the arguments passed to the program
///
/// @param argc Number of arguments
/// @param argv Array of arguments
void checkArguments(int argc, char *argv[]);

/// @brief Intended to be executed by a separate thread or process. Acts as a server to the sensor.
/// Handles the communication with it until the program ends. Doesn't end when the sensor is
/// disconnected, rather waits for a new sensor to be connected
///
/// @return void* Returns NULL always. It just exists to fill the required signature for a thread
/// intended function
void *connectToSensor();

/// @brief Handles the communication with the sensor. The life cycle of the function is the same as
/// the respective socket with the sensor
///
/// @param sensorSocket Socket used to communicate with the sensor
/// @param producer Kafka producer used to send the requests to the central
/// @param request Request to be sent to the central
/// @return true The program should continue
/// @return false The program should end
bool communicateWithSensor(int sensorSocket, rd_kafka_t *producer, Request *request);

/// @brief Handles kafka communications with the central
///
/// @return void* Returns NULL always. It just exists to fill the required signature for a thread
/// intended function
void *connectToCentral();

/// @brief Moves (if possible) the taxi to the next step. This function is dependent of the sensor
/// handler thread as it's continuously waiting the signal it to continue moving.
///
/// @return void* Returns NULL always. It just exists to fill the required signature for a thread
/// intended function
void *run();

/// @brief Gets the value of a global variable locking and unlocking a mutex
///
/// @param global Pointer to the global variable
/// @return bool Value of the global variable
bool getGlobal(bool *global);

/// @brief Calculates the distance between two coordinates taking into account that the map is
/// spherical (e.g. (0, 0) is next to (19, 19) the same way it is to (1, 1))
int sphericalDistance(Coordinate *a, Coordinate *b);

/// @brief Moves the taxi (changes pos) towards objective
void nextStep();

/// @brief Wrapper for sendEvent. Doesn't do anything else, just used because of readability
///
/// @param producer Kafka producer used to send the requests to the central
/// @param request Request to be sent to the central
void sendRequest(rd_kafka_t *producer, Request *request);

/// @brief Carries through the process of authentication with the central via socket
void authenticate();

/// @brief Intended to be executed by a separate thread or process. Continuously pings the central
/// to inform that the taxi is still active
void *ping();

/// @brief Sends the current state of the taxi to the ncurses GUI
void updateInfo();

int main(int argc, char *argv[]) {
  pthread_t thread_sensor;
  pthread_t thread_central;
  pthread_t thread_run;
  pthread_t ping_thread;

  pipe(gui_pipe);

  pid_t gui_pid = fork();
  if (gui_pid != 0) {
    close(gui_pipe[1]);
    ncursesGui(gui_pipe[0]);
    close(gui_pipe[0]);
    exit(0);
  }
  close(gui_pipe[0]);

  char buffer[BUFFER_SIZE];
  buffer[0] = PGUI_REGISTER_PROCESS;
  memcpy(buffer + 1, (pid_t[]){getpid()}, sizeof(pid_t));
  write(gui_pipe[1], buffer, BUFFER_SIZE);

  g_log_set_default_handler(ncurses_log_handler, (int[]){-1, gui_pipe[1]});

  checkArguments(argc, argv);

  updateInfo();

  pthread_mutex_init(&mut, NULL);
  pthread_mutex_init(&pos_mut, NULL);
  pthread_cond_init(&cond, NULL);
  pthread_cond_init(&socket_bound_cond, NULL);
  pthread_cond_init(&authenticated_cond, NULL);

  pthread_create(&thread_sensor, NULL, connectToSensor, NULL);

  authenticate();

  pthread_mutex_lock(&mut);
  pthread_cond_signal(&authenticated_cond);
  pthread_mutex_unlock(&mut);

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

  buffer[0] = PGUI_TAXI_FATAL_ERROR;
  write(gui_pipe[1], buffer, BUFFER_SIZE);

  return 0;
}

void updateInfo() {
  char buffer[BUFFER_SIZE];
  int offset = 0;

  pthread_mutex_lock(&mut);
  buffer[offset++] = PGUI_UPDATE_INFO;

  memcpy(buffer + offset, &pos, sizeof(Coordinate));
  offset += sizeof(Coordinate);
  memcpy(buffer + offset, &objective, sizeof(Coordinate));
  offset += sizeof(Coordinate);

  buffer[offset++] = service;
  buffer[offset++] = orderedToStop;
  buffer[offset++] = canMove;

  memcpy(buffer + offset, &importance, sizeof(IMPORTANCE));
  offset += sizeof(IMPORTANCE);
  memcpy(buffer + offset, &reason, sizeof(int));
  offset += sizeof(int);

  buffer[offset++] = sensorConnected;

  memcpy(buffer + offset, &lastOrder, sizeof(SUBJECT));
  offset += sizeof(SUBJECT);
  memcpy(buffer + offset, &lastOrderCoord, sizeof(Coordinate));
  offset += sizeof(Coordinate);

  buffer[offset++] = lastOrderCompleted;
  pthread_mutex_unlock(&mut);

  write(gui_pipe[1], buffer, BUFFER_SIZE);
}

void checkArguments(int argc, char *argv[]) {
  char usage[100];
  sprintf(usage, "Usage: %s <central IP:port> <kafka IP:port> <listen port> <id>", argv[0]);

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
  rd_kafka_t *consumer = createKafkaUser(&kafka, RD_KAFKA_CONSUMER, kafkaId);
  sprintf(kafkaId, "taxi-%d-central-producer", id);
  rd_kafka_t *producer = createKafkaUser(&kafka, RD_KAFKA_PRODUCER, kafkaId);
  rd_kafka_message_t *msg = NULL;
  subscribeToTopics(&consumer, (const char *[]){"taxi_responses"}, 1);
  Request request;
  pthread_mutex_lock(&mut);
  strcpy(request.session, session);
  request.id = id;
  pthread_mutex_unlock(&mut);

  while (!getGlobal(&stopProgram)) {
    if (msg != NULL)
      rd_kafka_message_destroy(msg);
    if (!(msg = poll_wrapper(consumer, 1000)))
      continue;

    memcpy(&response, msg->payload, sizeof(response));

    if (response.id != id || strcmp(session, response.session) != 0)
      continue;

    switch (response.subject) {
    case TRESPONSE_START_SERVICE:
      service = response.data[sizeof(Coordinate)];
      // Fallthrough intended
    case TRESPONSE_GOTO:
      pthread_mutex_lock(&mut);
      lastOrderCompleted = false;
      orderedToStop = false;
      if (!canMove) {
        request.subject = REQUEST_TAXI_CANT_MOVE_REMINDER;
        sendRequest(producer, &request);
      }
      memcpy(&objective, response.data, sizeof(Coordinate));
      lastOrderCoord = objective;
      lastOrder = TRESPONSE_GOTO;
      pthread_mutex_unlock(&mut);

      g_message("Central ordered to move to [%i, %i]", objective.x + 1, objective.y + 1);
      updateInfo();
      sleep(1); // Give time to central to process the request
      break;

    case TRESPONSE_STOP:
      pthread_mutex_lock(&mut);
      orderedToStop = true;
      lastOrder = TRESPONSE_STOP;
      pthread_mutex_unlock(&mut);
      g_message("Central ordered to stop");
      updateInfo();
      break;

    case TRESPONSE_CONTINUE:
      pthread_mutex_lock(&mut);
      lastOrder = TRESPONSE_CONTINUE;
      orderedToStop = false;
      if (!canMove) {
        request.subject = REQUEST_TAXI_CANT_MOVE_REMINDER;
        sendRequest(producer, &request);
      }
      pthread_mutex_unlock(&mut);
      g_message("Central ordered to continue");
      updateInfo();
      break;

    case TRESPONSE_CHANGE_POSITION:
      pthread_mutex_lock(&pos_mut);
      memcpy(&pos, response.data, sizeof(Coordinate));
      lastOrder = TRESPONSE_CHANGE_POSITION;
      lastOrderCoord = pos;
      pthread_mutex_unlock(&pos_mut);
      g_message("Central ordered to change position to [%i, %i]", pos.x + 1, pos.y + 1);
      updateInfo();
      break;

    case TRESPONSE_SERVICE_COMPLETED:
      service = -1;
      updateInfo();
      break;

    default:
      break;
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
  rd_kafka_t *producer = createKafkaUser(&kafka, RD_KAFKA_PRODUCER, kafkaId);
  Request request;

  pthread_mutex_lock(&mut);
  pthread_cond_signal(&socket_bound_cond);
  pthread_cond_wait(&authenticated_cond, &mut);
  pthread_mutex_unlock(&mut);

  pthread_mutex_lock(&mut);
  strcpy(request.session, session);
  request.id = id;
  pthread_mutex_unlock(&mut);

  while (!getGlobal(&stopProgram)) {
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

    pthread_mutex_lock(&mut);
    sensorConnected = true;
    pthread_mutex_unlock(&mut);
    updateInfo();

    if (!communicateWithSensor(sensorSocket, producer, &request)) {
      close(sensorSocket);
      close(server);
      g_debug("Sensor exiting...");
      return NULL;
    }

    pthread_mutex_lock(&mut);
    sensorConnected = false;
    if (canMove) {
      canMove = false;
      request.subject = REQUEST_TAXI_CANT_MOVE;
      sendRequest(producer, &request);
    }
    pthread_mutex_unlock(&mut);
    updateInfo();
    g_warning("Sensor disconnected");
  }

  return NULL;
}

bool communicateWithSensor(int sensorSocket, rd_kafka_t *producer, Request *request) {
  fd_set readfds;
  char buffer[BUFFER_SIZE];
  bool printedMsg = false;

  while (!getGlobal(&stopProgram)) {
    FD_ZERO(&readfds);
    FD_SET(sensorSocket, &readfds);
    if (select(sensorSocket + 1, &readfds, NULL, NULL,
               &(struct timeval){0, COURTESY_TIME * 1000}) <= 0) {
      g_warning("Error reading from socket");
      break;
    }

    if (read(sensorSocket, buffer, BUFFER_SIZE) <= 0) {
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
      request->subject = canMove ? REQUEST_TAXI_CAN_MOVE : REQUEST_TAXI_CANT_MOVE;
      sendRequest(producer, request);
    }
    memcpy(&importance, buffer + 3, sizeof(IMPORTANCE));
    memcpy(&reason, buffer + 3 + sizeof(IMPORTANCE), sizeof(int));

    if (buffer[2]) {
      request->subject = REQUEST_TAXI_FATAL_ERROR;
      sendRequest(producer, request);

      g_critical("Error couldn't be resolved. Terminating...");
      stopProgram = true;
      pthread_mutex_unlock(&mut);
      sleep(2);
      g_debug("Sleeped");
      pthread_mutex_lock(&mut);
      pthread_cond_signal(&cond);
      pthread_mutex_unlock(&mut);
      return false;
    }

    buffer[0] = STX;
    buffer[1] = orderedToStop;
    write(sensorSocket, buffer, BUFFER_SIZE);
    pthread_mutex_unlock(&mut);
    updateInfo();

    if (canMove && !getGlobal(&orderedToStop)) {
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
  rd_kafka_t *producer = createKafkaUser(&kafka, RD_KAFKA_PRODUCER, kafkaId);
  Request request;
  pthread_mutex_lock(&mut);
  strcpy(request.session, session);
  request.id = id;
  pthread_mutex_unlock(&mut);

  while (!getGlobal(&stopProgram)) {
    pthread_mutex_lock(&mut);
    pthread_cond_wait(&cond, &mut);
    pthread_mutex_unlock(&mut);

    if (getGlobal(&stopProgram))
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
      orderedToStop = true;
      lastOrderCompleted = true;
      pthread_mutex_unlock(&mut);

      request.subject = REQUEST_DESTINATION_REACHED;
      sendRequest(producer, &request);
    }

    updateInfo();
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
  int socket = connectToServer(&central);

#define auth_error(format, ...)                                                                    \
  buffer[0] = EOT;                                                                                 \
  write(socket, buffer, BUFFER_SIZE);                                                              \
  close(socket);                                                                                   \
  g_error(format, ##__VA_ARGS__);

#define auth_warning(msg, n)                                                                       \
  {                                                                                                \
    if (i == n) {                                                                                  \
      auth_error(msg ". Try limit reached");                                                       \
    }                                                                                              \
    g_warning(msg ". Retrying...");                                                                \
    continue;                                                                                      \
  }

  if (socket == -1) {
    auth_error("Error connecting to central");
  }

  for (int i = 0; i < 5; i++) {
    buffer[0] = ENQ;
    g_debug("Sending ENQ");
    if (!write(socket, buffer, BUFFER_SIZE)) {
      auth_warning("Error writing to central", 4);
    }

    g_debug("Waiting for response");
    if (!read(socket, buffer, BUFFER_SIZE)) {
      auth_warning("Error reading from central", 4);
    }

    if (buffer[0] == ACK) {
      g_debug("Received ACK");
      break;
    }

    if (buffer[0] == NACK) {
      g_debug("Received NACK");
      auth_warning("Connection refused", 4);
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
    if (!write(socket, buffer, BUFFER_SIZE)) {
      auth_warning("Error writing to central", 2);
    }

    g_debug("Waiting for response");
    if (!read(socket, buffer, BUFFER_SIZE)) {
      auth_warning("Error reading from central", 2);
    }

    if (buffer[0] != STX || buffer[2 + UUID_LENGTH] != ETX) {
      g_debug("Etx or stx issue");
      auth_warning("Invalid message received", 2);
    }

    int lrc = 0;
    for (int i = 0; i < 2 + UUID_LENGTH + 1; i++) {
      lrc ^= buffer[i];
    }
    if (lrc != buffer[2 + UUID_LENGTH + 1]) {
      g_debug("lrc issue");
      auth_warning("Invalid message received", 2);
    }

    if (buffer[1] == ACK) {
      g_debug("Received ACK");
      g_message("Authentication successful. ID assigned: %i", id);
      memcpy(session, buffer + 2, UUID_LENGTH);
      break;
    }

    if (buffer[1] != NACK) {
      g_debug("Content issue");
      auth_warning("Invalid message received", 2);
    }

    auth_error("Authentication failed. Id %i is already in use", id);
    break;
  }

  buffer[0] = EOT;
  write(socket, buffer, BUFFER_SIZE);
  close(socket);
}

void *ping() {
  rd_kafka_t *localProducer = createKafkaUser(&kafka, RD_KAFKA_PRODUCER, "customer-ping-producer");
  Request request;
  pthread_mutex_lock(&mut);
  strcpy(request.session, session);
  request.subject = PING_TAXI;
  request.id = id;
  pthread_mutex_unlock(&mut);

  while (!getGlobal(&stopProgram)) {
    sendEvent(localProducer, "requests", &request, sizeof(Request));
    usleep(PING_CADENCE * 1000 * 1000);
  }

  g_debug("Ping exiting...");
  return NULL;
}