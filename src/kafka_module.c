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
static Request request;
static Response response;

void respond() {
  sendEvent(producer, "responses", &response, sizeof(response));
}

void startKafkaServer() {
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

    case ASK_FOR_SERVICE:
      if (!assignTaxi(request.id, request.coord)) {
        g_warning("Couldn't find a taxi to serve client '%c' at (%i, %i)",
                  request.id, request.coord.x, request.coord.y);
        response.subject = SERVICE_DENIED;
        respond();
      }
      break;

    case TAXI_MOVE:
      moveTaxi();
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
                          NULL, 0)) {
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
  char query[200];

  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      response.map[i][j].agent = NONE;
      response.map[i][j].str[0] = '\0';
    }
  }

  strcpy(query, "SELECT id, x, y FROM locations");
  if (mysql_query(conn, query)) {
    g_warning("Error loading locations: %s", mysql_error(conn));
  } else {
    r_locations = mysql_store_result(conn);
    while ((row = mysql_fetch_row(r_locations))) {
      x = atoi(row[1]);
      y = atoi(row[2]);
      response.map[x][y].agent = LOCATION;
      strcpy(response.map[x][y].str, row[0]);
    }
  }

  strcpy(query,
         "SELECT id, x, y FROM clients c WHERE NOT EXISTS (SELECT 1 "
         "FROM taxis t WHERE t.client = c.id AND t.carrying_client = true)");
  if (mysql_query(conn, query)) {
    g_warning("Error loading clients: %s", mysql_error(conn));
  } else {
    r_clients = mysql_store_result(conn);
    while ((row = mysql_fetch_row(r_clients))) {
      x = atoi(row[1]);
      y = atoi(row[2]);
      response.map[x][y].agent = CLIENT;
      strcpy(response.map[x][y].str, row[0]);
    }
  }

  strcpy(query, "SELECT id, x, y, moving, client, carrying_client FROM taxis");
  if (mysql_query(conn, query)) {
    g_warning("Error loading taxis: %s", mysql_error(conn));
  } else {
    r_taxis = mysql_store_result(conn);
    while ((row = mysql_fetch_row(r_taxis))) {
      x = atoi(row[1]);
      y = atoi(row[2]);
      strcpy(response.map[x][y].str, row[0]);

      if (atoi(row[3]))
        response.map[x][y].agent = TAXI;
      else
        response.map[x][y].agent = TAXI_STOPPED;

      if (atoi(row[5]) && row[4] != NULL)
        strcat(response.map[x][y].str, row[4]);
    }
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

void moveTaxi() {
  char query[200];

  sprintf(query, "UPDATE taxis SET x = %i, y = %i WHERE id = %i",
          request.coord.x, request.coord.y, request.id);
  if (mysql_query(conn, query) || mysql_affected_rows(conn) == 0) {
    g_warning("Error updating taxi %i: %s", request.id, mysql_error(conn));
    return;
  }

  g_message("Taxi %d moved to (%i, %i)", request.id, request.coord.x,
            request.coord.y);

  response.subject = MAP_UPDATE;
  loadMap();
  respond();
}

bool assignTaxi(char clientId, Coordinate clientCoord) {
  MYSQL_RES *result = NULL;
  MYSQL_ROW row;
  char query[200];
  char destination = request.extraArgs[0];

  sprintf(query,
          "INSERT INTO clients (id, destination, x, y) VALUES ('%c', "
          "'%c', %i, %i)",
          clientId, destination, clientCoord.x, clientCoord.y);
  if (mysql_query(conn, query)) {
    g_warning("Error inserting client %c: %s", clientId, mysql_error(conn));
    return false; // TODO: not the same as the following falses cause client
                  // couldn't be added
  }

  strcpy(query,
         "SELECT id FROM taxis WHERE connected = true AND available = true");
  if (mysql_query(conn, query)) {
    g_warning("Error checking id: %s\n", mysql_error(conn));
    return false;
  }

  result = mysql_store_result(conn);

  if (mysql_num_rows(result) == 0) {
    return false;
  }

  row = mysql_fetch_row(result);

  sprintf(query,
          "UPDATE taxis SET available = false, moving = true, client = "
          "'%c' WHERE id = %i",
          clientId, atoi(row[0]));
  if (mysql_query(conn, query)) {
    g_warning("Error updating taxi %i: %s", atoi(row[0]), mysql_error(conn));
    return false;
  }

  response.clientId = clientId;
  response.taxiId = atoi(row[0]);
  response.subject = SERVICE_ACCEPTED;
  response.adressee = ANY;
  memcpy(response.extraArgs, &clientCoord, sizeof(Coordinate));
  g_message("Taxi %d will serve client '%c' at (%i, %i)", response.taxiId,
            response.clientId, clientCoord.x, clientCoord.y);
  respond();

  return true;
}