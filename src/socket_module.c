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
Request request;

void listenSocket(int listenPort) {
  int serverSocket = openSocket(listenPort);
  int counter = 0;

  struct sockaddr_in address;
  socklen_t addrlen = sizeof(address);

  g_message("Listening on port %i", listenPort);

  while (true) {
    g_debug("Waiting connections");
    int clientSocket =
        accept(serverSocket, (struct sockaddr *)&address, &addrlen);

    if (clientSocket == -1) {
      g_warning("Error accepting connection");
      continue;
    }

    struct timeval timeout = {5, 0};

    if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
                   sizeof(timeout)) < 0) {
      g_warning("Error setting socket timeout");
      close(clientSocket);
      continue;
    }

    pid_t pid = fork();
    if (pid == 0) {
      close(serverSocket);
      attend((int[]){clientSocket, counter});
      exit(0);
    }
    close(clientSocket);

    counter++;
  }
}

void attend(void *args) {
  int clientSocket = ((int *)args)[0];
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
    if (read(clientSocket, buffer, BUFFER_SIZE) < 1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        g_critical("%sTimeout reached on request %i", prefix, counter);
        break;
      } else {
        g_warning("%sError reading from the socket", prefix);
        g_debug("%sSending NACK", prefix);
        buffer[0] = NACK;
        write(clientSocket, buffer, BUFFER_SIZE);
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
          mysql_real_connect(conn, db.ip, "root", DB_PASSWORD, DB_NAME, db.port,
                             NULL, 0)) {
        g_debug("%sSent ACK", prefix);
        buffer[0] = ACK;
      } else {
        g_warning("%sCouldn't connect to database", prefix);
        g_debug("%sSent NACK", prefix);
        buffer[0] = NACK;
      }
      write(clientSocket, buffer, BUFFER_SIZE);
      break;

    case STX:
      g_debug("%sReceived STX", prefix);
      int id;
      memcpy(&id, buffer + 1, sizeof(id));

      int newId = conn != NULL ? checkId(conn, id) : -1;

      if (newId != -1) {
        g_message("%sAssigned ID %i", prefix, newId);
      } else {
        g_warning("%sCouldn't assign ID %i. No more IDs available.", prefix,
                  id);
      }

      producer = createKafkaAgent(&kafka, RD_KAFKA_PRODUCER);
      request.subject = NEW_TAXI;
      request.id = id + TAXI_ID_OFFSET;
      sendEvent(producer, "requests", &request, sizeof(request));
      g_message("Updated map");

      buffer[0] = newId == id ? ACK : NACK;
      memcpy(buffer + 1, &newId, sizeof(id));

      write(clientSocket, buffer, BUFFER_SIZE);
      break;

    default:
      g_debug("Received: %i", buffer[0]);
      g_warning("%sUknown message received", prefix);
      buffer[0] = NACK;
      write(clientSocket, buffer, BUFFER_SIZE);
    }
  }

  g_message("%sClosing connection", prefix);
  close(clientSocket);
  mysql_close(conn);
  mysql_library_end();
  pthread_exit(NULL);
}

int checkId(MYSQL *conn, int id) {
  MYSQL_RES *result = NULL;
  MYSQL_RES *result2 = NULL;
  MYSQL_ROW row;
  char query[100];

#define SAVE_ID(_id)                                                           \
  {                                                                            \
    mysql_free_result(result);                                                 \
    mysql_free_result(result2);                                                \
    sprintf(query, "INSERT INTO taxis (id) VALUES (%i)", _id);                 \
    if (mysql_query(conn, query)) {                                            \
      g_warning("Error inserting id: %s\n", mysql_error(conn));                \
      return -1;                                                               \
    }                                                                          \
    return _id;                                                                \
  }

  sprintf(query, "SELECT id FROM taxis WHERE id = %i", id);
  if (mysql_query(conn, query)) {
    g_warning("Error checking id: %s\n", mysql_error(conn));
    return -1;
  }

  result = mysql_store_result(conn);

  if (mysql_num_rows(result) == 0) {
    SAVE_ID(id);
  }

  sprintf(query, "SELECT id FROM taxis");
  if (mysql_query(conn, query)) {
    g_warning("Error checking id: %s\n", mysql_error(conn));
    return -1;
  }

  int i = 0;
  result2 = mysql_store_result(conn);
  while ((row = mysql_fetch_row(result2))) {
    if (i != atoi(row[0])) {
      SAVE_ID(i);
    }
    i++;
  }

  if (i >= 100)
    return -1;
  SAVE_ID(i);
}
