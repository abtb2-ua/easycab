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

void printRandomFillerMessage();
void checkArguments(int argc, char *argv[], char *fileName);
void readFile(char *fileName, char *services);
void connectToCentral(rd_kafka_t *auth_consumer);
void askService(char service);
void *ping();

int main(int argc, char *argv[]) {
  rd_kafka_t *auth_consumer;
  char fileName[100];
  char services[100];
  char kafkaId[50];

  g_log_set_default_handler(log_handler, NULL);
  checkArguments(argc, argv, fileName);
  readFile(fileName, services);

  sprintf(kafkaId, "customer-%c-producer", id);
  producer = createKafkaAgent(&kafka, RD_KAFKA_PRODUCER, kafkaId);
  sprintf(kafkaId, "customer-%c-consumer", id);

  consumer = createKafkaAgent(&kafka, RD_KAFKA_CONSUMER, kafkaId);
  auth_consumer =
      createKafkaAgent(&kafka, RD_KAFKA_CONSUMER, generate_unique_id());
  subscribeToTopics(&consumer, (const char *[]){"customer_responses"}, 1);
  subscribeToTopics(&auth_consumer, (const char *[]){"customer_responses"}, 1);

  // Wait for metadata to load
  g_message("Loading...");
  poll_wrapper(auth_consumer, 1000);
  connectToCentral(auth_consumer);

  pthread_t thread;
  pthread_create(&thread, NULL, ping, NULL);
  pthread_detach(thread);

  for (int i = 0; i < strlen(services); i++) {
    g_message("Asking for service to go to %c...", services[i]);
    askService(services[i]);
    printRandomFillerMessage();
    sleep(4);
  }

  g_message("There are no more services. Goodbye!");

  request.subject = REQUEST_DISCONNECT_CLIENT;
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
    g_error("Invalid kafka port (%i), must be between 0 and 65535. %s",
            kafka.port, usage);
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

void connectToCentral(rd_kafka_t *auth_consumer) {
  rd_kafka_message_t *msg = NULL;
  request.subject = REQUEST_NEW_CLIENT;
  request.id = id;
  request.coord = pos;
  g_message("Sending request for connection");
  strcpy(request.data, generate_unique_id());
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
        (response.subject != CRESPONSE_CONFIRMATION &&
         response.subject != CRESPONSE_ERROR)) {
      // g_debug("Id received: %c", response.id);
      // g_debug("Subject received: %i", response.subject);
      // g_debug("Unique id received: %s", response.data);
      // g_debug("Unique id: %s", request.data);
      g_debug("Message not for us");
      continue;
    }

    if (response.subject == CRESPONSE_ERROR)
      g_error("Central rejected the connection");

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
        g_message(
            "There aren't any available taxis! You've been added to queue");
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
      "Shopping at the mall...",
      "Borrowing a book from the library...",
      "Buying a Halloween costume...",
      "Doing some chores...",
      "Doing daily workout at the gym...",
      "Visiting my grandma...",
      "Strolling around downtown...",
      "Watching a movie...",
      "\"Studying\" for the exam...",
      "Doing a job interview...",
  };

  g_message("%s\n", messages[rand() % 10]);
}

void *ping() {
  rd_kafka_t *localProducer =
      createKafkaAgent(&kafka, RD_KAFKA_PRODUCER, "customer-ping-producer");
  Request request;
  request.subject = PING_CUSTOMER;
  request.id = id;

  while (true) {
    sendEvent(localProducer, "requests", &request, sizeof(Request));
    usleep(PING_CADENCE * 1000 * 1000);
  }
}