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
#define MAP_SIZE 160

#define DB_NAME "db"
#define DB_PASSWORD "1234"

#define INCONVENIENCES_COUNT 10

#define PING_CADENCE 1 // seconds
// Time the server will wait before checking strays if RESET_DB is false
#define PING_GRACE_TIME 2 // seconds
// Threshold for the server to disconnect a user
#define USER_GRACE_TIME 3 // seconds

extern const char *inconveniences[4][INCONVENIENCES_COUNT];

#define store_result_wrapper(result)                                           \
  result = mysql_store_result(conn);                                           \
  mysql_next_result(conn);

enum NCURSES_GUI_PROTOCOL {
  PGUI_WRITE_TOP_WINDOW,
  PGUI_WRITE_BOTTOM_WINDOW,
  PGUI_END_EXECUTION,
  PGUI_REGISTER_PROCESS
};

typedef enum AGENT_TYPE { TAXI = 0, CLIENT, LOCATION } AGENT_TYPE;
typedef enum AGENT_STATUS {
  TAXI_EMPTY = 0,
  TAXI_CARRYING_CLIENT,
  TAXI_STOPPED,
  TAXI_DISCONNECTED,
  CLIENT_WAITING_TAXI,
  CLIENT_IN_TAXI,
  CLIENT_IN_QUEUE,
  CLIENT_OTHER
} AGENT_STATUS;

typedef enum SUBJECT {
  ORDER_GOTO,
  ORDER_STOP,
  ORDER_CONTINUE,

  STRAY_CUSTOMER,
  STRAY_TAXI,
  PING_CUSTOMER,
  PING_TAXI,

  REQUEST_NEW_TAXI,
  REQUEST_NEW_CLIENT,
  REQUEST_TAXI_RECONNECT,
  REQUEST_DESTINATION_REACHED,
  REQUEST_ASK_FOR_SERVICE,
  REQUEST_TAXI_MOVE,
  REQUEST_TAXI_CANT_MOVE,
  REQUEST_TAXI_CAN_MOVE,
  REQUEST_TAXI_CANT_MOVE_REMINDER,
  REQUEST_TAXI_FATAL_ERROR,
  REQUEST_DISCONNECT_TAXI,
  REQUEST_DISCONNECT_CLIENT,

  CRESPONSE_SERVICE_ACCEPTED,
  CRESPONSE_SERVICE_DENIED,
  CRESPONSE_ERROR,
  CRESPONSE_CONFIRMATION,
  CRESPONSE_PICKED_UP,
  CRESPONSE_SERVICE_COMPLETED,
  CRESPONSE_TAXI_RESUMED,
  CRESPONSE_TAXI_STOPPED,
  CRESPONSE_TAXI_DISCONNECTED,

  TRESPONSE_STOP,
  TRESPONSE_GOTO,
  TRESPONSE_CONTINUE,
  TRESPONSE_CHANGE_POSITION,

  MRESPONSE_MAP_UPDATE,
} SUBJECT;

typedef enum IMPORTANCE { MINOR = 0, NORMAL, MAJOR, FATAL } IMPORTANCE;

typedef struct Coordinate {
  int x, y;
} Coordinate;

typedef struct Agent {
  AGENT_TYPE type;
  AGENT_STATUS status;
  Coordinate coord;
  int id;
  char obj; // Customer's destination or taxi's service
  bool carryingCustomer;
  bool canMove;
} Agent;

typedef struct Address {
  char ip[20];
  int port;
} Address;

typedef struct Request {
  SUBJECT subject;
  Coordinate coord;
  int id;
  char data[40];
} Request;

typedef struct Response {
  SUBJECT subject;
  // 100 max taxis, 30 max clients, 30 max locations
  // It will never will be full as there are 25 letters allowed for clients and
  // locations ids
  int map[MAP_SIZE];
  char id;
  char data[40];
} Response;

void log_handler(const gchar *log_domain, GLogLevelFlags log_level,
                 const gchar *message, gpointer user_data);
int openSocket(int port);
int connectToServer(Address *server);
char *generate_unique_id();
rd_kafka_t *createKafkaAgent(Address *server, rd_kafka_type_t type, char *id);
void subscribeToTopics(rd_kafka_t **consumer, const char **topics,
                       int topicsCount);
void sendEvent(rd_kafka_t *producer, const char *topic, void *value,
               size_t valueSize);
rd_kafka_message_t *poll_wrapper(rd_kafka_t *rk, int timeout_ms);
int serializeAgent(Agent *agent);
void deserializeAgent(Agent *dest, int agent);

#endif