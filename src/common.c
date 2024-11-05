#include "common.h"
#include "glib.h"
#include <librdkafka/rdkafka.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// clang-format off
// There's a joke inconvenience in each group down below, try and find it!
const char *inconveniences[4][INCONVENIENCES_COUNT] ={
{
  "A kitty cat is confused in the middle of the road...",
  "Waiting for a granny to cross the street...",
  "The traffic light is red...", "There's a small traffic jam...",
  "Waiting for a chance to overtake a stopped car...",
  "A little kid has a very scary costume! Waiting for the taxi sensitive digital engine to regain composture...",
  "Kids playing football accidentally kicked the ball into the road...",
  "The road is in poor condition, slowing down...",
  "Dirt is blocking the sensor...",
  "There's construction work by the road, slowing down..."
}, {
  "There's an accident up ahead, traffic is barely moving...",
  "Stuck behind a slow-moving truck on a narrow road...",
  "Waiting for a long freight train to pass...",
  "Heavy traffic due to roadwork and lane closures...",
  "A car broke down in the middle of the road, waiting for it to be cleared...",
  "Navigating through a flooded street, moving carefully...",
  "A deer is blocking the road ahead...",
  "A blackout has left the road without proper lighting...",
  "The car's navigation needs recalibration due to a GPS error...",
  "There's a very convenient taxi costume stand by the road, and this taxi also likes Halloween!..."
}, {
  "A major accident has closed the highway, traffic is at a standstill...",
  "Severe snowstorm ahead, stopping as a precaution...",
  "Henry Cavill is taking a walk by the road, even the taxi wants a signature!...",
  "Traffic congestion during rush hour, moving at a crawl...",
  "Waiting for emergency services to clear the road...",
  "There has been a landslide up ahead, waiting for it to be cleared...",
  "An unexpected vehicle breakdown, waiting for a tow truck...",
  "Participating in a long traffic jam caused by a Halloween costume festival...",
  "Got a flat tire, waiting for insurance company to send support...",
  "The taxi's software suffered a major bug, it is being auto-repaired..."
}, {
  "The motor broke down!", "Battery failure, unable to operate!",
  "Critical software failure, losing control of the vehicle!",
  "Sensor fatal malfunction, unable to detect obstacles!",
  "Accidentally bumped into a confused bird. The poor taxi is traumatized "
  "and won't move!",
  "An unforeseen violent protest is happening ahead, and the taxi took the "
  "blame. It's not in a condition to continue!",
  "Heavy rain has damaged the taxi's system. It's not responding!",
  "Huge tree fell onto the road and the taxi unfortunately crashed. All "
  "passengers are okay!",
  "Taxi's circuits froze due to a malfunction in its insulation. It's not "
  "responding!",
  "There is a pretty serious traffic jam. Authorities are communicating it "
  "may take days to clear the road!"
}};
// clang-format on

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

  printf("%s[%02i:%02i:%02i.%03i]%s ", BLUE, hours, minutes, seconds,
         milliseconds, RESET);

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
  int opt = 1;

  if (s == -1) {
    g_error("Error opening socket.");
  }

  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("Error en setsockopt");
    close(s);
    exit(EXIT_FAILURE);
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

rd_kafka_t *createKafkaAgent(Address *serverAddress, rd_kafka_type_t type,
                             char *id) {
  rd_kafka_conf_t *conf;
  rd_kafka_t *agent;
  char errstr[512];
  // char client_id[50];
  char serverAddressStr[50];

  // sprintf(client_id, "kafka-%s-%s",
  //         type == RD_KAFKA_PRODUCER ? "producer" : "consumer",
  //         generate_unique_id());
  sprintf(serverAddressStr, "%s:%d", serverAddress->ip, serverAddress->port);

  conf = rd_kafka_conf_new();

  SET_CONFIG(conf, "log_level", "0", errstr);
  SET_CONFIG(conf, "bootstrap.servers", serverAddressStr, errstr);
  SET_CONFIG(conf, "client.id", id, errstr);
  SET_CONFIG(conf, "acks", "all", errstr);
  SET_CONFIG(conf, "auto.offset.reset", "earliest", errstr);
  if (type == RD_KAFKA_CONSUMER) {
    SET_CONFIG(conf, "group.id", id, errstr);
    SET_CONFIG(conf, "session.timeout.ms", "6000", errstr);
    SET_CONFIG(conf, "max.poll.interval.ms", "6000", errstr);
  }

  agent = rd_kafka_new(type, conf, errstr, sizeof(errstr));
  if (!agent) {
    g_error("Failed to create new Kafka agent: %s", errstr);
  }

  conf = NULL;

  if (type == RD_KAFKA_CONSUMER) {
    rd_kafka_poll_set_consumer(agent);
  }

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
  }
}

rd_kafka_message_t *poll_wrapper(rd_kafka_t *rk, int timeout_ms) {
  rd_kafka_message_t *msg = rd_kafka_consumer_poll(rk, timeout_ms);

  if (!msg) {
    g_debug("Nothing read");
    return NULL;
  }

  if (msg->err) {
    g_warning("Error: %s", rd_kafka_message_errstr(msg));
    return NULL;
  }

  long now = time(NULL);
  if (now - atol(msg->key) > 10 * 60) {
    g_debug("Message too old: sent %li seconds ago", now - atol(msg->key));
    return NULL;
  }

  return msg;
}

int serializeAgent(Agent *agent) {
  int mask1 = 0x01;
  int mask2 = 0x03;
  int mask3 = 0x07;
  int mask5 = 0x1F;
  int mask7 = 0x7F;
  int mask8 = 0xFF;

  int res = 0;
  res |= (agent->type & mask2);
  res |= (agent->canMove & mask1) << 2;
  res |= (agent->status & mask3) << 3;
  res |= (agent->coord.x & mask5) << 6;
  res |= (agent->coord.y & mask5) << 11;
  res |= (agent->id & mask7) << 16;
  res |= (agent->obj & mask8) << 23;
  res |= (agent->carryingCustomer & mask1) << 31;

  return res;
}

void deserializeAgent(Agent *dest, int agent) {
  int mask1 = 0x01;
  int mask2 = 0x03;
  int mask3 = 0x07;
  int mask5 = 0x1F;
  int mask7 = 0x7F;
  int mask8 = 0xFF;

  dest->type = agent & mask2;
  dest->canMove = (agent >> 2) & mask1;
  dest->status = (agent >> 3) & mask3;
  dest->coord.x = (agent >> 6) & mask5;
  dest->coord.y = (agent >> 11) & mask5;
  dest->id = (agent >> 16) & mask7;
  dest->obj = (agent >> 23) & mask8;
  dest->carryingCustomer = (agent >> 31) & mask1;
}