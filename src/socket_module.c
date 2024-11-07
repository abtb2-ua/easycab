#include "socket_module.h"
#include "common.h"
#include "glib.h"
#include <librdkafka/rdkafka.h>
#include <mysql/mysql.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

extern Address kafka, db;
extern int gui_pipe[2];
static rd_kafka_t *producer;
static Request request;
extern char session[UUID_LENGTH];

void listenSocket(int listenPort) {
  int serverSocket = openSocket(listenPort);
  int counter = 0;

  struct sockaddr_in address;
  socklen_t addrlen = sizeof(address);

  g_message("Listening on port %i", listenPort);

  while (true) {
    g_debug("Waiting connections");
    int customerSocket = accept(serverSocket, (struct sockaddr *)&address, &addrlen);

    if (customerSocket == -1) {
      g_warning("Error accepting connection");
      continue;
    }

    struct timeval timeout = {5, 0};

    if (setsockopt(customerSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) <
        0) {
      g_warning("Error setting socket timeout");
      close(customerSocket);
      continue;
    }

    // pid_t pid = fork();
    // if (pid == 0) {
    //   close(serverSocket);
    //   attend((int[]){customerSocket, counter});
    //   exit(0);
    // }
    // close(customerSocket);
    pthread_t thread;
    pthread_create(&thread, NULL, attend, (int[]){customerSocket, counter});
    pthread_detach(thread);

    counter++;
  }
}

void *attend(void *args) {
  int customerSocket = ((int *)args)[0];
  int counter = ((int *)args)[1];
  char buffer[BUFFER_SIZE];
  char prefix[20];
  bool continueLoop = true;

  mysql_library_init(0, NULL, NULL);
  MYSQL *conn = mysql_init(NULL);
  mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, (int[]){2});

  sprintf(prefix, "[request %i] ", counter);

  g_message("Processing authentication request %i", counter);

  while (continueLoop) {
    if (read(customerSocket, buffer, BUFFER_SIZE) < 1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        g_critical("%sTimeout reached on request %i", prefix, counter);
        break;
      } else {
        g_warning("%sError reading from the socket", prefix);
        g_debug("%sSending NACK", prefix);
        buffer[0] = NACK;
        write(customerSocket, buffer, BUFFER_SIZE);
        continue;
      }
    }

    switch (buffer[0]) {
    case EOT:
      g_debug("%sReceived EOT", prefix);
      continueLoop = false;
      break;

    case ENQ:
      g_debug("%sReceived ENQ", prefix);
      if (conn != NULL && mysql_ping(conn) &&
          mysql_real_connect(conn, db.ip, "root", DB_PASSWORD, DB_NAME, db.port, NULL,
                             CLIENT_MULTI_STATEMENTS)) {
        g_debug("%sSent ACK", prefix);
        buffer[0] = ACK;
      } else {
        g_warning("%sCouldn't connect to database", prefix);
        g_debug("%sSent NACK", prefix);
        buffer[0] = NACK;
      }
      write(customerSocket, buffer, BUFFER_SIZE);
      break;

    case STX:
      g_debug("%sReceived STX", prefix);
      int id;
      bool reconnected;
      memcpy(&id, buffer + 1, sizeof(id));

      bool idAvailable = conn != NULL ? checkId(conn, id, &reconnected) : false;

      if (idAvailable) {
        g_message("%sAssigned ID %i", prefix, id);

        char kafkaId[50];
        sprintf(kafkaId, "authenticate-central-%i-producer", counter);
        producer = createKafkaUser(&kafka, RD_KAFKA_PRODUCER, kafkaId);
        request.subject = reconnected ? REQUEST_TAXI_RECONNECT : REQUEST_NEW_TAXI;
        request.id = id;
        memcpy(request.session, session, UUID_LENGTH);
        sendEvent(producer, "requests", &request, sizeof(request));
        g_message("Updated map");
      }

      buffer[0] = STX;
      buffer[1] = idAvailable ? ACK : NACK;
      memcpy(buffer + 2, session, UUID_LENGTH);
      buffer[2 + UUID_LENGTH] = ETX;

      buffer[2 + UUID_LENGTH + 1] = 0;
      // LRC
      for (int i = 0; i < 2 + UUID_LENGTH + 1; i++) {
        buffer[2 + UUID_LENGTH + 1] ^= buffer[i];
      }

      write(customerSocket, buffer, BUFFER_SIZE);
      break;

    default:
      g_debug("Received: %i", buffer[0]);
      g_warning("%sUknown message received", prefix);
      buffer[0] = NACK;
      write(customerSocket, buffer, BUFFER_SIZE);
    }
  }

  g_message("%sClosing connection", prefix);
  close(customerSocket);
  mysql_close(conn);
  mysql_library_end();
  pthread_exit(NULL);

  return NULL;
}

bool checkId(MYSQL *conn, int id, bool *reconnected) {
  MYSQL_RES *result = NULL;
  MYSQL_ROW row;
  char query[200];

  sprintf(query, "CALL ConnectTaxi(%i)", id);

  g_debug("Query: %s", query);
  if (mysql_query(conn, query)) {
    g_warning("Error executing query %s: %s", query, mysql_error(conn));
    return false;
  }

  store_result_wrapper(result);
  row = mysql_fetch_row(result);
  if (row[0] == NULL) {
    g_warning("Unexpected error connecting taxi %i", id);
    return false;
  } else if (row[0][0] != '0' && row[0][0] != '1') {
    g_warning("Error connecting taxi %i: %s", id, row[0]);
    return false;
  } else if (row[0][0] == '1') {
    *reconnected = false;
  } else {
    *reconnected = true;
  }

  return true;
}
