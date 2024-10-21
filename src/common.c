#include "common.h"
#include "glib.h"
#include <librdkafka/rdkafka.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

char *generate_unique_id() {
  static char id[37];
  uuid_t binuuid;
  uuid_generate_random(binuuid);
  uuid_unparse(binuuid, id);
  return id;
}

void log_handler(const gchar *log_domain, GLogLevelFlags log_level,
                 const gchar *message, gpointer user_data) {
  const char *GREEN = "\x1b[38;5;156m";
  const char *RESET = "\x1b[0m";
  const char *BOLD = "\x1b[1m";
  const char *BLUE = "\x1b[38;5;75m";
  const char *ORANGE = "\x1b[38;5;215m";
  const char *PURPLE = "\x1b[38;5;135m";
  const char *RED = "\x1b[38;5;9m";
  const char *PINK = "\x1b[38;5;219m";

  if ((log_level & G_LOG_LEVEL_MASK) == G_LOG_LEVEL_DEBUG) {
    char *debug_env = getenv("G_MESSAGES_DEBUG");

    if (debug_env == NULL)
      return;
  }

  struct timeval now;
  gettimeofday(&now, NULL);
  int hours = (now.tv_sec / 3600) % 24 + 2; // UTC + 2
  int minutes = (now.tv_sec / 60) % 60;
  int seconds = (int)now.tv_sec % 60;
  int milliseconds = (int)now.tv_usec / 1000;

  printf("%s[%i:%i:%i.%i]%s ", BLUE, hours, minutes, seconds, milliseconds,
         RESET);

  switch (log_level & G_LOG_LEVEL_MASK) {
  case G_LOG_LEVEL_CRITICAL:
    printf("%s** CRITICAL **%s", PURPLE, RESET);
    break;
  case G_LOG_LEVEL_WARNING:
    printf("%s** WARNING **%s", ORANGE, RESET);
    break;
  case G_LOG_LEVEL_MESSAGE:
    printf("%s%sMessage%s", GREEN, BOLD, RESET);
    break;
  case G_LOG_LEVEL_DEBUG:
    printf("%s%sDebug%s", PINK, BOLD, RESET);
    break;
  case G_LOG_LEVEL_INFO:
    printf("%s%sInfo%s", BLUE, BOLD, RESET);
    break;
  case G_LOG_LEVEL_ERROR:
    printf("%s** ERROR **%s", RED, RESET);
    break;

  default:
    break;
  }

  printf(": %s\n", message);

  if (log_level == 6 || log_level == G_LOG_LEVEL_ERROR) {
    exit(1);
  }
}

void cleanUpSocket(int status, void *s) {
  if (*(int *)s != -1)
    close(*(int *)s);
}

int openSocket(int port) {
  struct sockaddr_in server;
  int s = socket(AF_INET, SOCK_STREAM, 0);

  if (s == -1) {
    g_error("Error opening socket.");
  }

  on_exit(cleanUpSocket, &s);

  server.sin_family = AF_INET;
  server.sin_port = htons(port);
  server.sin_addr.s_addr = INADDR_ANY;
  if (bind(s, (struct sockaddr *)&server, sizeof(server)) == -1) {
    close(s);
    s = -1;
    g_error("Error binding socket.");
  }

  g_message("Socket bound");

  if (listen(s, 4) == -1) {
    close(s);
    s = -1;
    g_error("Error listening.");
  }

  return s;
}

int connectToServer(Address *serverAddress) {
  struct sockaddr_in server;
  int s = socket(AF_INET, SOCK_STREAM, 0);

  if (s == -1) {
    g_error("Error opening socket");
  }

  g_message("Socket opened");

  on_exit(cleanUpSocket, &s);

  server.sin_addr.s_addr = inet_addr(serverAddress->ip);
  server.sin_family = AF_INET;
  server.sin_port = htons(serverAddress->port);

  if (connect(s, (struct sockaddr *)&server, sizeof(server)) == -1) {
    close(s);
    s = -1;
    g_error("Error connecting to server");
  }

  g_message("Connected to server");

  return s;
}

#define SET_CONFIG(conf, key, value, errstr)                                   \
  if (rd_kafka_conf_set(conf, key, value, errstr, sizeof(errstr)) !=           \
      RD_KAFKA_CONF_OK)                                                        \
    g_error("Error configuring Kafka: %s", errstr);

rd_kafka_t *createKafkaAgent(Address *serverAddress, rd_kafka_type_t type) {
  rd_kafka_conf_t *conf;
  rd_kafka_t *agent;
  char errstr[512];
  char client_id[50];
  char serverAddressStr[50];
  char *id = generate_unique_id();

  sprintf(client_id, "kafka-%s-%s",
          type == RD_KAFKA_PRODUCER ? "producer" : "consumer", id);
  sprintf(serverAddressStr, "%s:%d", serverAddress->ip, serverAddress->port);

  conf = rd_kafka_conf_new();

  SET_CONFIG(conf, "log_level", "0", errstr);
  SET_CONFIG(conf, "bootstrap.servers", serverAddressStr, errstr);
  SET_CONFIG(conf, "client.id", client_id, errstr);
  SET_CONFIG(conf, "acks", "all", errstr);
  SET_CONFIG(conf, "auto.offset.reset", "earliest", errstr);
  if (type == RD_KAFKA_CONSUMER) {
    SET_CONFIG(conf, "group.id", client_id, errstr);
    SET_CONFIG(conf, "session.timeout.ms", "600000", errstr);
    SET_CONFIG(conf, "max.poll.interval.ms", "600000", errstr);
  }

  agent = rd_kafka_new(type, conf, errstr, sizeof(errstr));
  if (!agent) {
    g_error("Failed to create new Kafka agent: %s", errstr);
  }

  conf = NULL;

  if (type == RD_KAFKA_CONSUMER) {
    rd_kafka_poll_set_consumer(agent);
  }

  g_message("%s named %s created",
            type == RD_KAFKA_PRODUCER ? "Producer" : "Consumer", client_id);

  return agent;
}

void subscribeToTopics(rd_kafka_t **consumer, const char **topics,
                       int topicsCount) {
  rd_kafka_topic_partition_list_t *subscription =
      rd_kafka_topic_partition_list_new(topicsCount);
  rd_kafka_resp_err_t err;

  for (int i = 0; i < topicsCount; i++) {
    rd_kafka_topic_partition_list_add(subscription, topics[i],
                                      RD_KAFKA_PARTITION_UA);
  }

  err = rd_kafka_subscribe(*consumer, subscription);
  if (err) {
    rd_kafka_topic_partition_list_destroy(subscription);
    rd_kafka_destroy(*consumer);
    g_error("Failed to subscribe to %d topics: %s", subscription->cnt,
            rd_kafka_err2str(err));
  }

  rd_kafka_topic_partition_list_destroy(subscription);

  char log[512] = "Subscribed to topics: ";

  for (int i = 0; i < topicsCount; i++) {
    strcat(log, topics[i]);
    if (i < topicsCount - 1)
      strcat(log, ", ");
  }
}

void sendEvent(rd_kafka_t *producer, const char *topic, void *value,
               size_t valueSize) {
  rd_kafka_resp_err_t err;
  char key[20];
  sprintf(key, "%li", time(NULL));

  if (producer == NULL)
    g_error("Producer is NULL");

  g_debug("Sending message to topic %s", topic);
  err = rd_kafka_producev(
      producer, RD_KAFKA_V_TOPIC(topic),
      RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
      RD_KAFKA_V_KEY(key, strlen(key)), // Ensure key is not NULL
      RD_KAFKA_V_VALUE(value, valueSize), RD_KAFKA_V_OPAQUE(NULL),
      RD_KAFKA_V_END);

  if (err) {
    g_error("Failed to produce to topic %s: %s", topic, rd_kafka_err2str(err));
  }

  rd_kafka_poll(producer, 100);

  rd_kafka_flush(producer, 5 * 1000);

  if (rd_kafka_outq_len(producer) > 0) {
    g_error("The message was not delivered.");
  } else {
    g_debug("The message was delivered successfully.");
  }
}

rd_kafka_message_t *poll_wrapper(rd_kafka_t *rk, int timeout_ms) {
  rd_kafka_message_t *msg = rd_kafka_consumer_poll(rk, timeout_ms);

  if (!msg) {
    g_debug("Nothing read");
    return NULL;
  }

  if (msg->err) {
    g_debug("Error: %s", rd_kafka_message_errstr(msg));
    return NULL;
  }

  long now = time(NULL);
  if (now - atol(msg->key) > 10) {
    g_debug("Message too old: sent %li seconds ago", now - atol(msg->key));
    return NULL;
  }

  return msg;
}