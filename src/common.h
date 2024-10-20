#ifndef COMMON_H
#define COMMON_H

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <glib.h>
#include <librdkafka/rdkafka.h>
#include <math.h>
#include <mysql/mysql.h>
#include <ncurses.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>
#include <uuid/uuid.h>

typedef struct Address {
  char ip[20];
  int port;
} Address;

enum COLORS {
  PASTEL_RED = 100,
  PASTEL_GREEN,
  PASTEL_YELLOW,
  PASTEL_BLUE,
  PASTEL_MAGENTA,
  PASTEL_PURPLE,
  PASTEL_ORANGE
};

// Since the communication will be done through pipes, we won't use STX nor ETX.
// Thus, there's no problem if enumerated values overlap with them
enum NCURSES_GUI_PROTOCOL {
  PGUI_WRITE_TOP_WINDOW,
  PGUI_WRITE_BOTTOM_WINDOW,
  PGUI_END_EXECUTION,
  PGUI_REGISTER_PROCESS
};

#define ENQ 0x05
#define ACK 0x06
#define NACK 0x15
#define EOT 0x04
#define STX 0x02
#define ETX 0x03

#define BUFFER_SIZE 50

void log_handler(const gchar *log_domain, GLogLevelFlags log_level,
                 const gchar *message, gpointer user_data);
int openSocket(int port);
int connectToServer(Address *server);
rd_kafka_t *createKafkaAgent(Address *server, rd_kafka_type_t type);
void subscribeToTopics(rd_kafka_t **consumer, const char **topics,
                       int topicsCount);
void sendEvent(rd_kafka_t *producer, const char *topic, char *key, char *value,
               size_t valueSize);

#endif