#include "kafka_module.h"
#include "common.h"
#include "glib.h"
#include <librdkafka/rdkafka.h>
#include <mysql/mysql.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum RESPONSE_TOPICS { RESPONSE_CUSTOMER, RESPONSE_TAXI, RESPONSE_MAP };

extern Address db, kafka;

static rd_kafka_t *producer;
static rd_kafka_t *consumer;
static MYSQL *conn;
static Response response;

void respond(enum RESPONSE_TOPICS topic) {
  char *topicName = (topic == RESPONSE_CUSTOMER) ? "customer_responses"
                    : (topic == RESPONSE_TAXI)   ? "taxi_responses"
                                                 : "map_responses";
  loadMap();
  sendEvent(producer, topicName, &response, sizeof(response));
}

void startKafkaServer() {
  Request request;
  rd_kafka_message_t *msg = NULL;

  pthread_t thread;
  pthread_create(&thread, NULL, checkStrays, NULL);
  pthread_detach(thread);

  init();

  response.subject = MRESPONSE_MAP_UPDATE;
  respond(RESPONSE_MAP);
  g_message("Sent initial map to responses topic");

  while (true) {
    if (msg != NULL)
      rd_kafka_message_destroy(msg);
    if (!(msg = poll_wrapper(consumer, 1000)))
      continue;

    memcpy(&request, msg->payload, sizeof(Request));

    switch (request.subject) {
    case REQUEST_NEW_TAXI:
      g_message("New taxi registered. Updating the map...");
      response.subject = MRESPONSE_MAP_UPDATE;
      respond(RESPONSE_MAP);

      checkQueue();
      break;

    case REQUEST_NEW_CLIENT:
      insertCustomer(&request);
      break;

    case REQUEST_TAXI_RECONNECT:
      resumePosition(&request);
      refreshTaxiInstructions(&request, true);
      break;

    case STRAY_TAXI:
    case REQUEST_TAXI_FATAL_ERROR:
      disconnectTaxi(&request);
      break;

    case STRAY_CUSTOMER:
      disconnectCustomer(&request);
      break;

    case PING_CUSTOMER:
    case PING_TAXI:
      refreshLastUpdate(&request);
      break;

    case REQUEST_ASK_FOR_SERVICE:
      processServiceRequest(&request);
      break;

    case REQUEST_DESTINATION_REACHED:
      refreshTaxiInstructions(&request, false);
      break;

    case REQUEST_TAXI_MOVE:
      moveTaxi(&request);
      break;

    case REQUEST_DISCONNECT_TAXI:
      disconnectTaxi(&request);
      break;

    case REQUEST_DISCONNECT_CLIENT:
      disconnectCustomer(&request);
      break;

    case REQUEST_TAXI_CANT_MOVE:
    case REQUEST_TAXI_CAN_MOVE:
      setTaxiCanMove(request.id, request.subject == REQUEST_TAXI_CAN_MOVE);
      break;

    case REQUEST_TAXI_CANT_MOVE_REMINDER:
      g_message("Taxi %i should be moving but it's currently unable to. "
                "Waiting until its error gets solved...",
                request.id);
      break;

    case ORDER_GOTO:
    case ORDER_STOP:
    case ORDER_CONTINUE:
      sendOrder(&request);
      break;

    default:
      g_debug("Unhandled subject: %i", request.subject);
      break;
    }
  }
}

void init() {
  char kafkaId[50];
  sprintf(kafkaId, "central-producer");
  producer = createKafkaAgent(&kafka, RD_KAFKA_PRODUCER, kafkaId);
  sprintf(kafkaId, "central-consumer");
  consumer = createKafkaAgent(&kafka, RD_KAFKA_CONSUMER, kafkaId);

  subscribeToTopics(&consumer, (const char *[]){"requests"}, 1);

  mysql_library_init(0, NULL, NULL);
  conn = mysql_init(NULL);

  if (!mysql_real_connect(conn, db.ip, "root", DB_PASSWORD, DB_NAME, db.port,
                          NULL, CLIENT_MULTI_STATEMENTS)) {
    mysql_close(conn);
    g_error("Error connecting to database");
  }

  signal(SIGINT, cleanUp);
}

void loadMap() {
  MYSQL_RES *r_locations = NULL;
  MYSQL_RES *r_customers = NULL;
  MYSQL_RES *r_taxis = NULL;
  MYSQL_ROW row;
  int index = 0;
  Agent agent;

  g_debug("Query: CALL LoadMap()");
  if (mysql_query(conn, "CALL LoadMap()")) {
    g_warning("Error loading map: %s", mysql_error(conn));
    return;
  }

  store_result_wrapper(r_locations);
  store_result_wrapper(r_customers);
  store_result_wrapper(r_taxis);

  agent.type = LOCATION;
  while ((row = mysql_fetch_row(r_locations))) {
    agent.id = row[0][0];
    agent.coord.x = atoi(row[1]);
    agent.coord.y = atoi(row[2]);

    response.map[index] = serializeAgent(&agent);
    index++;
  }

  agent.type = CLIENT;
  while ((row = mysql_fetch_row(r_customers))) {
    agent.id = row[0][0];
    agent.coord.x = atoi(row[1]);
    agent.coord.y = atoi(row[2]);
    agent.obj = row[3] ? row[3][0] : -1;
    agent.status = (row[3] == NULL ? CLIENT_OTHER
                    : atoi(row[4]) ? CLIENT_IN_QUEUE
                    : atoi(row[5]) ? CLIENT_IN_TAXI
                                   : CLIENT_WAITING_TAXI);

    response.map[index] = serializeAgent(&agent);
    index++;
  }

  agent.type = TAXI;
  while ((row = mysql_fetch_row(r_taxis))) {
    agent.id = atoi(row[0]);
    agent.coord.x = atoi(row[1]);
    agent.coord.y = atoi(row[2]);
    agent.obj = row[3] ? row[3][0] : -1;
    agent.status = (!atoi(row[6])   ? TAXI_DISCONNECTED
                    : !atoi(row[4]) ? TAXI_STOPPED
                    : atoi(row[5])  ? TAXI_CARRYING_CLIENT
                                    : TAXI_EMPTY);
    agent.carryingCustomer = atoi(row[5]);
    agent.canMove = atoi(row[7]);

    response.map[index] = serializeAgent(&agent);
    index++;
  }

  response.map[index] = 0;

  mysql_free_result(r_locations);
  mysql_free_result(r_customers);
  mysql_free_result(r_taxis);
}

void cleanUp() {
  rd_kafka_destroy(producer);
  rd_kafka_destroy(consumer);

  mysql_close(conn);
  mysql_library_end();
}

void moveTaxi(Request *request) {
  MYSQL_RES *err_result = NULL;
  MYSQL_ROW row;
  char query[200];

  sprintf(query, "CALL MoveTaxi(%i, %i, %i)", request->id, request->coord.x,
          request->coord.y);

  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return;
  }

  store_result_wrapper(err_result);
  row = mysql_fetch_row(err_result);

  if (row[0] != NULL) {
    g_warning("Error moving taxi %i: %s", request->id, row[0]);
  } else {
    g_message("Taxi %d moved to [%i, %i]", request->id, request->coord.x + 1,
              request->coord.y + 1);

    response.subject = MRESPONSE_MAP_UPDATE;
    respond(RESPONSE_MAP);
  }

  mysql_free_result(err_result);

  // sprintf(query, "UPDATE taxis SET x = %i, y = %i WHERE id = %i",
  //         request->coord.x, request->coord.y, request->id);
  // g_debug("Query: %s", query);
  // if (mysql_query(conn, query) || mysql_affected_rows(conn) == 0) {
  //   g_warning("Error updating taxi %i: %s", request->id, mysql_error(conn));
  //   return;
  // }

  // g_message("Taxi %d moved to [%i, %i]", request->id, request->coord.x + 1,
  //           request->coord.y + 1);

  // response.subject = MRESPONSE_MAP_UPDATE;
  // ();
  // respond(RESPONSE_MAP);
}

void insertCustomer(Request *request) {
  MYSQL_RES *result = NULL;
  MYSQL_ROW row;
  char query[200];
  sprintf(query, "CALL InsertCustomer('%c', %i, %i)", request->id,
          request->coord.x, request->coord.y);
  response.id = request->id;
  strcpy(response.data, request->data);

  g_debug("Query: %s", query);
  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return;
  }

  store_result_wrapper(result);
  row = mysql_fetch_row(result);
  if (row[0] == NULL) {
    g_message("Inserted customer '%c'", request->id);
    response.subject =
        CRESPONSE_CONFIRMATION; // TODO: Bug in customer where if we try to init
                                // to customers with the same id, both receive
                                // error and disconnect

    respond(RESPONSE_CUSTOMER);
  } else {
    g_warning("Error inserting customer '%c': %s", request->id, row[0]);
    response.subject = CRESPONSE_ERROR;
    respond(RESPONSE_CUSTOMER);
  }

  g_debug("Unique id: %s", response.data);

  mysql_free_result(result);
}

void processServiceRequest(Request *request) {
  MYSQL_RES *err_result = NULL;
  MYSQL_RES *result = NULL;
  MYSQL_RES *queue_result = NULL;
  MYSQL_ROW row;
  char destination = request->data[0];
  char customerId = request->id;
  char query[200];

  sprintf(query, "CALL AssignTaxi('%c', '%c')", customerId, destination);

  g_debug("Query: %s", query);
  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return;
  }

  store_result_wrapper(err_result);
  row = mysql_fetch_row(err_result);

  if (row[0] != NULL) {
    g_warning("Error assigning taxi to customer '%c': %s", customerId, row[0]);
    response.subject = CRESPONSE_SERVICE_DENIED;
    response.id = customerId;
    response.data[0] = false;
    respond(RESPONSE_CUSTOMER);
  } else {
    store_result_wrapper(result);
    row = mysql_fetch_row(result);

    if (row[0] == NULL) {
      g_message(
          "There aren't any available taxis. Adding customer '%c' to queue",
          customerId);

      sprintf(query, "CALL AddToQueue('%c')", customerId);
      g_debug("Query: %s", query);
      if (mysql_query(conn, query)) {
        g_warning("Error executing query %s: %s", query, mysql_error(conn));
        return;
      }

      store_result_wrapper(queue_result);
      row = mysql_fetch_row(queue_result);

      response.subject = CRESPONSE_SERVICE_DENIED;
      response.id = customerId;

      if (row[0] != NULL) {
        g_warning("Error adding customer '%c' to queue: %s", customerId,
                  row[0]);
        response.data[0] = false;
        respond(RESPONSE_CUSTOMER);
        return;
      }

      response.data[0] = true;
      respond(RESPONSE_CUSTOMER);
      return;
    }

    g_message("Service accepted. Taxi %s assigned to customer '%c'", row[2],
              customerId);

    response.subject = CRESPONSE_SERVICE_ACCEPTED;
    response.id = customerId;

    Coordinate customerCoord = {.x = atoi(row[0]), .y = atoi(row[1])};
    int taxiId = atoi(row[2]);

    memcpy(response.data, &taxiId, sizeof(int));

    respond(RESPONSE_CUSTOMER);

    response.subject = TRESPONSE_GOTO;
    response.id = taxiId;
    memcpy(response.data, &customerCoord, sizeof(Coordinate));
    g_message("Ordering taxi %i to go to [%i, %i]", taxiId, customerCoord.x + 1,
              customerCoord.y + 1);
    respond(RESPONSE_TAXI);
  }

  mysql_free_result(err_result);
  mysql_free_result(result);
}

void refreshTaxiInstructions(Request *request, bool reconnected) {
  MYSQL_RES *err_result = NULL;
  MYSQL_RES *result = NULL;
  MYSQL_ROW row;
  char query[200];

  sprintf(query, "CALL GetTaxiStatus(%i)", request->id);

  g_debug("Query: %s", query);
  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return;
  }

  store_result_wrapper(err_result);
  row = mysql_fetch_row(err_result);

  if (row[0] != NULL) {
    g_warning("Error getting taxi status: %s", row[0]);
  } else {
    store_result_wrapper(result);
    row = mysql_fetch_row(result);
    int status = atoi(row[0]);

    if (status == 0 || status == 3) {
      if (!reconnected) {
        g_message("Taxi %i has arrived to [%i, %i]", request->id,
                  atoi(row[1]) + 1, atoi(row[2]) + 1);
      }
      if (status == 0) {
        response.subject = MRESPONSE_MAP_UPDATE;

        respond(RESPONSE_MAP);
        sprintf(query, "UPDATE taxis SET available = TRUE WHERE id = %i",
                request->id);
        if (mysql_query(conn, query)) {
          g_warning("Error executing query %s: %s", query, mysql_error(conn));
          return;
        }
        checkQueue();
      } else {
        Coordinate coord = {.x = atoi(row[1]), .y = atoi(row[2])};
        g_message("Taxi will resume its service by going to [%i, %i]",
                  coord.x + 1, coord.y + 1);
        response.subject = TRESPONSE_GOTO;
        memcpy(response.data, &coord, sizeof(Coordinate));
        respond(RESPONSE_TAXI);
      }
    } else if (status == 1) {
      pickUpCustomer(request);
    } else {
      completeService(request);
    }
  }

  mysql_free_result(err_result);
  mysql_free_result(result);
}

void pickUpCustomer(Request *request) {
  MYSQL_RES *err_result = NULL;
  MYSQL_RES *result = NULL;
  MYSQL_ROW row;
  char query[200];

  sprintf(query, "CALL PickUpCustomer(%i)", request->id);

  g_debug("Query: %s", query);
  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return;
  }

  store_result_wrapper(err_result);
  row = mysql_fetch_row(err_result);

  if (row[0] != NULL) {
    g_warning("Error picking up taxi %i's customer: %s", request->id, row[0]);
  } else {
    store_result_wrapper(result);
    row = mysql_fetch_row(result);
    g_message("Customer '%s' picked up by taxi %i. They are now going towards "
              "location %s",
              row[0], request->id, row[1]);

    response.subject = CRESPONSE_PICKED_UP;
    response.id = row[0][0];
    memcpy(response.data, &request->id, sizeof(int));

    respond(RESPONSE_CUSTOMER);

    Coordinate locationCoord = {.x = atoi(row[2]), .y = atoi(row[3])};
    response.subject = TRESPONSE_GOTO;
    response.id = request->id;
    memcpy(response.data, &locationCoord, sizeof(Coordinate));
    respond(RESPONSE_TAXI);
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

  g_debug("Query: %s", query);
  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return;
  }

  store_result_wrapper(err_result);
  row = mysql_fetch_row(err_result);

  if (row[0] != NULL) {
    g_warning("Error picking up taxi %i's customer: %s", request->id, row[0]);
  } else {
    store_result_wrapper(result);
    row = mysql_fetch_row(result);
    g_message(
        "Customer '%s' service has been completed. Taxi %i left the customer "
        "on the location %s [%i, %i] and is now available",
        row[0], request->id, row[1], atoi(row[2]) + 1, atoi(row[3]) + 1);

    response.subject = CRESPONSE_SERVICE_COMPLETED;
    response.id = row[0][0];

    respond(RESPONSE_CUSTOMER);

    checkQueue();
  }

  mysql_free_result(err_result);
  mysql_free_result(result);
}

void checkQueue() {
  MYSQL_RES *result = NULL;
  MYSQL_ROW row;
  char query[200];

  sprintf(query, "CALL GetCustomerFromQueue()");
  g_debug("Query: %s", query);
  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return;
  }

  store_result_wrapper(result);
  row = mysql_fetch_row(result);

  if (row[0] == NULL) {
    g_message("There aren't any customers in queue");
    return;
  }

  Request request;
  request.subject = REQUEST_ASK_FOR_SERVICE;
  request.id = row[0][0];
  request.data[0] = row[1][0];
  processServiceRequest(&request);
}

void disconnectCustomer(Request *request) {
  char query[200];

  sprintf(query, "DELETE FROM customers WHERE id = '%c'", request->id);

  g_debug("Query: %s", query);
  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return;
  }

  if (mysql_affected_rows(conn) == 0) {
    g_warning("Error disconnecting customer '%c': No rows affected",
              request->id);
    return;
  }

  g_message("Customer '%c' disconnected", request->id);
  response.subject = MRESPONSE_MAP_UPDATE;

  respond(RESPONSE_MAP);
}

void disconnectTaxi(Request *request) {
  MYSQL_RES *err_result = NULL;
  MYSQL_RES *result = NULL;
  MYSQL_ROW row;
  char query[200];

  sprintf(query, "CALL DisconnectTaxi(%i)", request->id);

  g_debug("Query: %s", query);
  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return;
  }

  store_result_wrapper(err_result);
  row = mysql_fetch_row(err_result);

  if (row[0] != NULL) {
    g_warning("Error disconnecting taxi %i: %s", request->id, row[0]);
  } else {
    store_result_wrapper(result);
    row = mysql_fetch_row(result);

    g_message("Taxi %i disconnected", request->id);
    response.subject = MRESPONSE_MAP_UPDATE;
    respond(RESPONSE_MAP);

    if (row[0] != NULL) {
      int customerId = atoi(row[0]);
      Coordinate coord = {.x = atoi(row[1]), .y = atoi(row[2])};
      response.subject = CRESPONSE_TAXI_DISCONNECTED;
      response.id = customerId;
      memcpy(response.data, &request->id, sizeof(int));
      memcpy(response.data + sizeof(int), &coord, sizeof(Coordinate));
      respond(RESPONSE_CUSTOMER);
    }

    checkQueue();
  }

  mysql_free_result(err_result);
  mysql_free_result(result);
}

void sendOrder(Request *request) {
  char query[200];
  response.id = request->id;
  if (request->subject == ORDER_GOTO) {
    response.subject = TRESPONSE_GOTO;
    sprintf(query, "UPDATE taxis SET available = FALSE WHERE id = %i",
            request->id);
    if (mysql_query(conn, query)) {
      g_warning("Error executing query %s: %s", query, mysql_error(conn));
      return;
    }

    memcpy(response.data, &request->coord, sizeof(Coordinate));
    respond(RESPONSE_TAXI);
    g_message("Sent order to taxi %i to go to [%i, %i]", request->id,
              request->coord.x + 1, request->coord.y + 1);
  }

  MYSQL_RES *err_result = NULL;
  MYSQL_RES *result = NULL;
  MYSQL_ROW row;

  sprintf(query, "CALL ChangeMotion(%i, %i)", request->id,
          request->subject == ORDER_STOP ? false : true);

  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return;
  }

  store_result_wrapper(err_result);
  row = mysql_fetch_row(err_result);

  sprintf(query, "SELECT customer FROM taxis WHERE id = %i", request->id);
  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return;
  }

  store_result_wrapper(result);
  row = mysql_fetch_row(result);

  if (row[0] != NULL) {
    response.subject = request->subject == ORDER_STOP ? CRESPONSE_TAXI_RESUMED
                                                      : CRESPONSE_TAXI_STOPPED;
    response.id = row[0][0];
    respond(RESPONSE_CUSTOMER);
  }

  if (row[0] != NULL) {
    g_warning("Error changing motion: %s", row[0]);
  } else {
    response.subject =
        request->subject == ORDER_STOP ? TRESPONSE_STOP : TRESPONSE_CONTINUE;
    loadMap();
    respond(RESPONSE_TAXI);
    g_message("Sent order to taxi %i to %s", request->id,
              request->subject == ORDER_STOP ? "stop" : "continue moving");
  }

  mysql_free_result(err_result);
  mysql_free_result(result);
}

void resumePosition(Request *request) {
  MYSQL_RES *result = NULL;
  MYSQL_ROW row;
  char query[200];

  sprintf(query, "SELECT x, y FROM taxis WHERE id = %i", request->id);

  g_debug("Query: %s", query);
  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return;
  }

  store_result_wrapper(result);
  if (mysql_num_rows(result) == 0) {
    g_warning("Error resuming position of taxi %i: Taxi not found",
              request->id);
    return;
  }
  row = mysql_fetch_row(result);

  if (row[0] == NULL) {
    g_warning("Error resuming position of taxi %i", request->id);
  } else {
    Coordinate coord = {.x = atoi(row[0]), .y = atoi(row[1])};
    g_message("Taxi %i resumed its service from [%i, %i]", request->id,
              coord.x + 1, coord.y + 1);
    response.id = request->id;
    response.subject = TRESPONSE_CHANGE_POSITION;
    memcpy(response.data, &coord, sizeof(Coordinate));
    g_message("Ordering taxi to resume its position");
    respond(RESPONSE_TAXI);
  }

  mysql_free_result(result);
}

void setTaxiCanMove(int taxiId, bool canMove) {
  MYSQL_RES *result = NULL;
  MYSQL_ROW row;
  char query[200];

  sprintf(query, "UPDATE taxis SET can_move = %i WHERE id = %i", canMove,
          taxiId);
  g_debug("Query: %s", query);
  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return;
  }

  if (mysql_affected_rows(conn) == 0) {
    g_warning("Error updating taxi %i", taxiId);
    return;
  }

  sprintf(query, "SELECT customer FROM taxis WHERE id = %i", taxiId);
  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return;
  }

  store_result_wrapper(result);
  row = mysql_fetch_row(result);

  if (row[0] != NULL) {
    response.subject =
        canMove ? CRESPONSE_TAXI_RESUMED : CRESPONSE_TAXI_STOPPED;
    response.id = row[0][0];
    respond(RESPONSE_CUSTOMER);
  }

  if (canMove) {
    g_message("Taxi %i can move (again)", taxiId);
  } else {
    g_message("Taxi %i suffered an error and can't move", taxiId);
  }

  response.subject = MRESPONSE_MAP_UPDATE;
  respond(RESPONSE_MAP);

  mysql_free_result(result);
}

void *checkStrays() {
  MYSQL *localConn = mysql_init(NULL);
  MYSQL_RES *result = NULL;
  MYSQL_ROW row;
  rd_kafka_t *producer = createKafkaAgent(&kafka, RD_KAFKA_PRODUCER,
                                          "central-stray-check-producer");
  Request request;

  bool resetDb = strcmp(getenv("RESET_DB"), "true") == 0;

  // Give some time to the server to process the pings
  if (!resetDb) {
    usleep(PING_GRACE_TIME * 1000 * 1000);
  }

  if (!mysql_real_connect(localConn, db.ip, "root", DB_PASSWORD, DB_NAME,
                          db.port, NULL, CLIENT_MULTI_STATEMENTS)) {
    mysql_close(localConn);
    g_warning("Error connecting to database. Strays check will be disabled");
    return NULL;
  }

  while (true) {
    usleep(USER_GRACE_TIME * 0.5 * 1000 * 1000);
    char query[200];

    sprintf(query, "CALL CheckStrays(%i)", USER_GRACE_TIME);
    if (mysql_query(localConn, query)) {
      g_warning("Error executing query %s: %s", query, mysql_error(localConn));
      continue;
    }

    for (int i = 0; i < 2; i++) {
      result = mysql_store_result(localConn);
      mysql_next_result(localConn);

      while ((row = mysql_fetch_row(result))) {
        request.subject = atoi(row[0]) ? STRAY_TAXI : STRAY_CUSTOMER;
        request.id = request.subject == STRAY_TAXI ? atoi(row[1]) : row[1][0];
        sendEvent(producer, "requests", &request, sizeof(Request));
      }

      mysql_free_result(result);
    }
  }

  return NULL;
}

void refreshLastUpdate(Request *request) {
  char query[200];

  if (request->subject == PING_CUSTOMER) {
    sprintf(query, "UPDATE customers SET last_update = NOW() WHERE id = '%c'",
            request->id);
  } else {
    sprintf(query, "UPDATE taxis SET last_update = NOW() WHERE id = %i",
            request->id);
  }

  g_debug("Query: %s", query);
  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return;
  }
}