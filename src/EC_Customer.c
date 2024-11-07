#include "common.h"
#include "glib.h"
#include <librdkafka/rdkafka.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// Communication variables
static Address kafka;
static rd_kafka_t *producer;
static rd_kafka_t *consumer;
static Request request;
static Response response;

// Customer variables
static char id;
static Coordinate pos;

void sendRequest() { sendEvent(producer, "requests", &request, sizeof(request)); }

/// @brief Prints a random filler message while waiting to ask for the next service
void printRandomFillerMessage();

/// @brief Checks the arguments passed to the program
///
/// @param argc Number of arguments
/// @param argv Array of arguments
/// @param fileName Output argument. File name to read the services from
void checkArguments(int argc, char *argv[], char *fileName);

/// @brief Reads the file containing the services to be asked
///
/// @param fileName File name to read the services from
/// @param services Output argument. Array of services to be asked
void readFile(char *fileName, char *services);

/// @brief Carries through the authentication process with the central
void connectToCentral();

/// @brief Asks for a service and handles the communication with the central until the service is
/// completed
///
/// @param service Service to be asked
void askService(char service);

/// @brief Intended to be executed by a separate thread or process. Continuously pings the central
/// to inform that the customer is still active
///
/// @param session Session id of the system
/// @return void* Returns NULL always. It just exists to fill the required signature for a thread
/// intended function
void *ping(void *session);

int main(int argc, char *argv[]) {
  char fileName[100];
  char services[100];
  char kafkaId[50];

  g_log_set_default_handler(log_handler, NULL);
  checkArguments(argc, argv, fileName);
  readFile(fileName, services);

  sprintf(kafkaId, "customer-%c-producer", id);
  producer = createKafkaUser(&kafka, RD_KAFKA_PRODUCER, kafkaId);
  sprintf(kafkaId, "customer-%c-consumer", id);

  consumer = createKafkaUser(&kafka, RD_KAFKA_CONSUMER, kafkaId);

  subscribeToTopics(&consumer, (const char *[]){"customer_responses"}, 1);

  // Wait for metadata to load
  g_message("Loading...");
  poll_wrapper(consumer, 1000);

  connectToCentral();

  pthread_t thread;
  pthread_create(&thread, NULL, ping, request.session);
  pthread_detach(thread);

  for (int i = 0; i < strlen(services); i++) {
    g_message("Asking for service to go to %c...", services[i]);
    askService(services[i]);
    printRandomFillerMessage();
    sleep(4);
  }

  g_message("There are no more services. Goodbye!");

  request.subject = REQUEST_DISCONNECT_CUSTOMER;
  sendRequest();

  g_message("Exiting...");
  return 0;
}

void checkArguments(int argc, char *argv[], char *fileName) {
  char usage[100];

  sprintf(usage, "Usage: %s <Kafka IP:Port> <ID> <File name> <x> <y>", argv[0]);

  if (argc < 6) {
    g_error("%s", usage);
  }

  if (sscanf(argv[1], "%[^:]:%d", kafka.ip, &kafka.port) != 2)
    g_error("Invalid kafka address. %s", usage);

  if (sscanf(argv[2], "%c", &id) != 1)
    g_error("Invalid id. %s", usage);

  if (sscanf(argv[3], "%s", fileName) != 1)
    g_error("Invalid file name. %s", usage);

  if (sscanf(argv[4], "%i", &pos.x) != 1)
    g_error("Invalid x. %s", usage);

  if (sscanf(argv[5], "%i", &pos.y) != 1)
    g_error("Invalid y. %s", usage);

  if (pos.x < 1 || pos.y < 1 || pos.x > 20 || pos.y > 20)
    g_error("Invalid position. Must be between 1 and 20. %s", usage);

  pos.x--;
  pos.y--;

  if (id < 'a' || id > 'z')
    g_error("Invalid id, must be between 'a' and 'z'. %s", usage);

  if (kafka.port < 1 || kafka.port > 65535)
    g_error("Invalid kafka port (%i), must be between 0 and 65535. %s", kafka.port, usage);
}

void readFile(char *fileName, char *services) {
  FILE *file = fopen(fileName, "r");
  char line[10];

  if (file == NULL)
    g_error("Couldn't open file %s", fileName);

  int i = 0;

  while (fgets(line, 10, file) != NULL) {
    if (line[0] == '#')
      continue;

    if (line[0] == '\n')
      continue;

    services[i] = line[0];
    i++;
  }

  services[i] = '\0';
  fclose(file);
}

void connectToCentral() {
  rd_kafka_t *auth_consumer = createKafkaUser(&kafka, RD_KAFKA_CONSUMER, NULL);
  subscribeToTopics(&auth_consumer, (const char *[]){"customer_responses"}, 1);

  rd_kafka_message_t *msg = NULL;
  request.subject = REQUEST_NEW_CUSTOMER;
  request.id = id;
  request.coord = pos;
  g_message("Sending request for connection");
  generate_unique_id(request.data);
  sendRequest();

  g_message("Waiting for confirmation");
  for (int i = 0;;) {
    if (!(msg = rd_kafka_consumer_poll(auth_consumer, 1000))) {
      i++;
      if (i == 5)
        g_error("Couldn't connect to central");
      g_debug("Nothing read");
      continue;
    }

    if (msg->err) {
      g_warning("Error: %s", rd_kafka_message_errstr(msg));
      continue;
    }

    memcpy(&response, msg->payload, sizeof(response));

    if (response.id != id || strcmp(response.data, request.data) != 0 ||
        (response.subject != CRESPONSE_CONFIRMATION && response.subject != CRESPONSE_ERROR)) {
      // g_debug("Id received: %c", response.id);
      // g_debug("Subject received: %i", response.subject);
      // g_debug("Unique id received: %s", response.data);
      // g_debug("Unique id: %s", request.data);
      g_debug("Message not for us");
      continue;
    }

    if (response.subject == CRESPONSE_ERROR)
      g_error("Central rejected the connection");

    memcpy(request.session, response.session, UUID_LENGTH);
    g_message("Central accepted the connection");
    break;
  }
}

void askService(char service) {
  rd_kafka_message_t *msg = NULL;
  request.subject = REQUEST_ASK_FOR_SERVICE;
  request.data[0] = service;

  sendEvent(producer, "requests", &request, sizeof(request));

  while (true) {
    if (msg != NULL)
      rd_kafka_message_destroy(msg);
    if (!(msg = poll_wrapper(consumer, 1000)))
      continue;

    memcpy(&response, msg->payload, sizeof(response));

    if (response.id != id)
      continue;

    switch (response.subject) {
    case CRESPONSE_SERVICE_ACCEPTED: {
      int taxiId;
      memcpy(&taxiId, response.data, sizeof(int));
      g_message("Service accepted. Taxi %i is coming for you", taxiId);
      break;
    }
    case CRESPONSE_TAXI_RESUMED:
      g_message("Taxi can move again. Resuming service...");
      break;
    case CRESPONSE_TAXI_STOPPED:
      g_warning("An unexpected error affected the taxi and it stopped. Waiting "
                "for it to resume...");
      break;

    case CRESPONSE_TAXI_DISCONNECTED: {
      int taxiId;
      Coordinate coord;
      memcpy(&taxiId, response.data, sizeof(int));
      memcpy(&coord, response.data + sizeof(int), sizeof(Coordinate));
      g_message("Taxi %i can't continue the service. You have been left in "
                "[%i, %i] waiting for a new service.",
                taxiId, coord.x + 1, coord.y + 1);
      break;
    }

    case CRESPONSE_SERVICE_DENIED:
      if (response.data[0] == true)
        g_message("There aren't any available taxis! You've been added to queue");
      else
        g_error("Service denied");
      break;
    case CRESPONSE_PICKED_UP: {
      int taxiId;
      memcpy(&taxiId, response.data, sizeof(int));
      g_message("Taxi %i has picked you up", taxiId);
      break;
    }
    case CRESPONSE_SERVICE_COMPLETED:
      g_message("We've arrived to %c", service);
      return;
    default:
      g_debug("Unhandled subject: %i", response.subject);
      break;
    }
  }
}

void printRandomFillerMessage() {
  char *messages[10] = {
      "Shopping at the mall...",           "Borrowing a book from the library...",
      "Buying a Halloween costume...",     "Doing some chores...",
      "Doing daily workout at the gym...", "Visiting my grandma...",
      "Strolling around downtown...",      "Watching a movie...",
      "\"Studying\" for the exam...",      "Doing a job interview...",
  };

  g_message("%s\n", messages[rand() % 10]);
}

void *ping(void *session) {
  rd_kafka_t *localProducer = createKafkaUser(&kafka, RD_KAFKA_PRODUCER, "customer-ping-producer");
  Request request;
  request.subject = PING_CUSTOMER;
  memcpy(request.session, session, UUID_LENGTH);
  request.id = id;

  while (true) {
    g_debug("Sending PING");
    sendEvent(localProducer, "requests", &request, sizeof(Request));
    usleep(PING_CADENCE * 1000 * 1000);
  }
}