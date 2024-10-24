#ifndef COMMON_H
#define COMMON_H

#include <arpa/inet.h>
#include <glib.h>
#include <librdkafka/rdkafka.h>
#include <mysql/mysql.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <threads.h>
#include <unistd.h>
#include <uuid/uuid.h>

#define ENQ 0x05
#define ACK 0x06
#define NACK 0x15
#define EOT 0x04
#define STX 0x02
#define ETX 0x03

#define BUFFER_SIZE 300
#define GRID_SIZE 20

#define DB_NAME "db"
#define DB_PASSWORD "1234"

#define store_result_wrapper(result)                                           \
  result = mysql_store_result(conn);                                           \
  mysql_next_result(conn);

enum COLORS {
  PASTEL_RED = 100,
  PASTEL_GREEN,
  PASTEL_YELLOW,
  PASTEL_BLUE,
  PASTEL_MAGENTA,
  PASTEL_PURPLE,
  PASTEL_ORANGE
};

enum NCURSES_GUI_PROTOCOL {
  PGUI_WRITE_TOP_WINDOW,
  PGUI_WRITE_BOTTOM_WINDOW,
  PGUI_END_EXECUTION,
  PGUI_REGISTER_PROCESS
};

typedef enum AGENT_TYPE {
  TAXI,
  TAXI_STOPPED,
  CLIENT,
  LOCATION,
  ANY,
  NONE
} AGENT_TYPE;

typedef enum SUBJECT {
  NEW_TAXI,
  NEW_CLIENT,
  ERROR,
  CONFIRMATION,
  ASK_FOR_SERVICE,
  TAXI_MOVE,
  MAP_UPDATE,
  // Used as response. Extra args: client's coordinates, client's destination
  // Coordinates
  SERVICE_ACCEPTED,
  SERVICE_DENIED,
  // Used as reuest.
  CLIENT_PICKED_UP,
  SERVICE_COMPLETED,
} SUBJECT;

typedef struct Coordinate {
  int x, y;
} Coordinate;

typedef struct Cell {
  AGENT_TYPE agent;
  char str[4];
} Cell;

typedef struct Address {
  char ip[20];
  int port;
} Address;

typedef struct Request {
  SUBJECT subject;
  Coordinate coord;
  int id;
  char extraArgs[20];
} Request;

typedef struct Response {
  SUBJECT subject;
  Cell map[GRID_SIZE][GRID_SIZE];
  char taxiId, clientId;
  char extraArgs[20];
} Response;

void log_handler(const gchar *log_domain, GLogLevelFlags log_level,
                 const gchar *message, gpointer user_data);
int openSocket(int port);
int connectToServer(Address *server);
rd_kafka_t *createKafkaAgent(Address *server, rd_kafka_type_t type);
void subscribeToTopics(rd_kafka_t **consumer, const char **topics,
                       int topicsCount);
void sendEvent(rd_kafka_t *producer, const char *topic, void *value,
               size_t valueSize);
rd_kafka_message_t *poll_wrapper(rd_kafka_t *rk, int timeout_ms);

#endif