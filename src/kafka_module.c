#include "kafka_module.h"
#include "common.h"
#include "glib.h"
#include <librdkafka/rdkafka.h>
#include <mysql/mysql.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

extern Address db, kafka;

static rd_kafka_t *producer;
static rd_kafka_t *consumer;
static MYSQL *conn;
static Response response;

void respond() {
  sendEvent(producer, "responses", &response, sizeof(response));
}

void startKafkaServer() {
  static Request request;
  rd_kafka_message_t *msg = NULL;
  init();

  response.subject = MAP_UPDATE;
  loadMap();
  respond();
  g_message("Sent empty map (only with locations) to responses topic");

  while (true) {
    if (msg != NULL)
      rd_kafka_message_destroy(msg);
    if (!(msg = poll_wrapper(consumer, 1000)))
      continue;

    memcpy(&request, msg->payload, sizeof(Request));

    switch (request.subject) {
    case NEW_TAXI:
      g_message("New taxi registered. Updating the map...");
      response.subject = MAP_UPDATE;
      loadMap();
      respond();
      break;

    case NEW_CLIENT:
      insertClient(&request);
      break;

    case ASK_FOR_SERVICE:
      processServiceRequest(&request);
      break;

    case CLIENT_PICKED_UP:
      pickUpClient(&request);
      // g_message("Client picked up");
      break;

    case SERVICE_COMPLETED:
      completeService(&request);
      // g_message("Service completed");
      break;

    case TAXI_MOVE:
      moveTaxi(&request);
      break;

    default:
      g_debug("Unhandled subject: %i", request.subject);
      break;
    }
  }
}

void init() {
  producer = createKafkaAgent(&kafka, RD_KAFKA_PRODUCER);
  consumer = createKafkaAgent(&kafka, RD_KAFKA_CONSUMER);

  subscribeToTopics(&consumer, (const char *[]){"requests"}, 1);

  mysql_library_init(0, NULL, NULL);
  conn = mysql_init(NULL);

  if (!mysql_real_connect(conn, db.ip, "root", DB_PASSWORD, DB_NAME, db.port,
                          NULL, CLIENT_MULTI_STATEMENTS)) {
    g_error("Error connecting to database");
  }

  signal(SIGINT, cleanUp);
}

void loadMap() {
  MYSQL_RES *r_locations = NULL;
  MYSQL_RES *r_clients = NULL;
  MYSQL_RES *r_taxis = NULL;
  MYSQL_ROW row;
  int x, y;

  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      response.map[i][j].agent = NONE;
      response.map[i][j].str[0] = '\0';
    }
  }

  g_debug("Query: CALL LoadMap()");
  if (mysql_query(conn, "CALL LoadMap()")) {
    g_warning("Error loading map: %s", mysql_error(conn));
    return;
  }

  store_result_wrapper(r_locations);
  store_result_wrapper(r_clients);
  store_result_wrapper(r_taxis);

  while ((row = mysql_fetch_row(r_locations))) {
    x = atoi(row[1]);
    y = atoi(row[2]);
    response.map[x][y].agent = LOCATION;
    strcpy(response.map[x][y].str, row[0]);
  }

  while ((row = mysql_fetch_row(r_clients))) {
    x = atoi(row[1]);
    y = atoi(row[2]);
    response.map[x][y].agent = CLIENT;
    strcpy(response.map[x][y].str, row[0]);
  }

  while ((row = mysql_fetch_row(r_taxis))) {
    x = atoi(row[1]);
    y = atoi(row[2]);
    g_debug("x: %i, y: %i", x, y);
    strcpy(response.map[x][y].str, row[0]);

    if (atoi(row[3]))
      response.map[x][y].agent = TAXI;
    else
      response.map[x][y].agent = TAXI_STOPPED;

    if (atoi(row[5]) && row[4] != NULL)
      strcat(response.map[x][y].str, row[4]);
  }

  mysql_free_result(r_locations);
  mysql_free_result(r_clients);
  mysql_free_result(r_taxis);
}

void cleanUp() {
  rd_kafka_destroy(producer);
  rd_kafka_destroy(consumer);

  mysql_close(conn);
  mysql_library_end();
}

void moveTaxi(Request *request) {
  char query[200];

  sprintf(query, "UPDATE taxis SET x = %i, y = %i WHERE id = %i",
          request->coord.x, request->coord.y, request->id);
  g_debug("Query: %s", query);
  if (mysql_query(conn, query) || mysql_affected_rows(conn) == 0) {
    g_warning("Error updating taxi %i: %s", request->id, mysql_error(conn));
    return;
  }

  g_message("Taxi %d moved to [%i, %i]", request->id, request->coord.x + 1,
            request->coord.y + 1);

  response.subject = MAP_UPDATE;
  loadMap();
  respond();
}

void insertClient(Request *request) {
  MYSQL_RES *result = NULL;
  MYSQL_ROW row;
  char query[200];
  sprintf(query, "CALL InsertClient('%c', %i, %i)", request->id,
          request->coord.x, request->coord.y);

  g_debug("Query: %s", query);
  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return;
  }

  store_result_wrapper(result);
  row = mysql_fetch_row(result);
  if (row[0] == NULL) {
    g_message("Inserted client %c", request->id);
    response.clientId = request->id;
    response.taxiId = -1;
    response.subject = CONFIRMATION;
    loadMap();
    respond();
  } else {
    g_message("Error inserting client %c: %s", request->id, row[0]);
    response.clientId = request->id;
    response.taxiId = -1;
    response.subject = ERROR;
    respond();
  }

  mysql_free_result(result);
}

void processServiceRequest(Request *request) {
  MYSQL_RES *err_result = NULL;
  MYSQL_RES *result = NULL;
  MYSQL_ROW row;
  char destination = request->extraArgs[0];
  char id = request->id;
  char query[200];

  sprintf(query, "CALL AssignTaxi('%c', '%c')", id, destination);

  g_debug("Query: %s", query);
  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return;
  }

  store_result_wrapper(err_result);
  row = mysql_fetch_row(err_result);

  if (row[0] != NULL) {
    g_warning("Error assigning taxi to client %c: %s", id, row[0]);
    response.subject = SERVICE_DENIED;
    response.clientId = id;
    response.taxiId = -1;
    respond();
  } else {
    store_result_wrapper(result);
    row = mysql_fetch_row(result);
    g_message("Service accepted. Taxi %s assigned to client %c", row[4], id);

    response.subject = SERVICE_ACCEPTED;
    response.taxiId = atoi(row[4]);
    response.clientId = id;

    Coordinate clientCoord = {.x = atoi(row[0]), .y = atoi(row[1])};
    Coordinate destCoord = {.x = atoi(row[2]), .y = atoi(row[3])};
    memcpy(response.extraArgs, &clientCoord, sizeof(Coordinate));
    memcpy(response.extraArgs + sizeof(Coordinate), &destCoord,
           sizeof(Coordinate));

    loadMap();
    respond();
  }

  mysql_free_result(err_result);
  mysql_free_result(result);
}

void pickUpClient(Request *request) {
  MYSQL_RES *err_result = NULL;
  MYSQL_RES *result = NULL;
  MYSQL_ROW row;
  char query[200];

  sprintf(query, "CALL PickUpClient(%i)", request->id);

  if (mysql_query(conn, query)) {
    g_warning("Error executing query %c: %s", request->id, mysql_error(conn));
    return;
  }

  store_result_wrapper(err_result);
  row = mysql_fetch_row(err_result);

  if (row[0] != NULL) {
    g_warning("Error picking up taxi %i's client: %s", request->id, row[0]);
  } else {
    store_result_wrapper(result);
    row = mysql_fetch_row(result);
    g_message("Client %s picked up by taxi %i. They are now going towards "
              "location %s",
              row[0], request->id, row[1]);

    response.subject = CLIENT_PICKED_UP;
    response.clientId = row[0][0];
    response.taxiId = -1;
    loadMap();
    respond();
  }

  mysql_free_result(err_result);
  mysql_free_result(result);
}

void completeService(Request *request) {
  MYSQL_RES *err_result = NULL;
  MYSQL_RES *result = NULL;
  MYSQL_ROW row;
  char query[200];

  sprintf(query, "CALL CompleteService(%i)", request->id);

  if (mysql_query(conn, query)) {
    g_warning("Error executing query %c: %s", request->id, mysql_error(conn));
    return;
  }

  store_result_wrapper(err_result);
  row = mysql_fetch_row(err_result);

  if (row[0] != NULL) {
    g_warning("Error picking up taxi %i's client: %s", request->id, row[0]);
  } else {
    store_result_wrapper(result);
    row = mysql_fetch_row(result);
    g_message("Client %s's service has been completed. Taxi %i left the client "
              "on the location %s [%i, %i] and is now available",
              row[0], request->id, row[1], atoi(row[2]) + 1, atoi(row[3]) + 1);

    response.subject = SERVICE_COMPLETED;
    response.clientId = row[0][0];
    response.taxiId = -1;
    loadMap();
    respond();
  }

  mysql_free_result(err_result);
  mysql_free_result(result);
}