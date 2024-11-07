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

// Constants used in authentication protocol
#define ENQ 0x05
#define ACK 0x06
#define NACK 0x15
#define EOT 0x04
#define STX 0x02
#define ETX 0x03

// Constants used in communication
#define BUFFER_SIZE 300
#define GRID_SIZE 20 // map dimensions
#define MAP_SIZE 160 // size of the array used in communications to store the map

// Parameters used in the database connection
#define DB_NAME "db"
#define DB_PASSWORD "1234"

// Number of different inconvenience messages for each importance level
#define INCONVENIENCES_COUNT 10

// Length of the session id
#define UUID_LENGTH 37

// Inconvenience messages
extern const char *inconveniences[4][INCONVENIENCES_COUNT];

// In seconds, time the users will wait before sending the next ping
#define PING_CADENCE 1

// In seconds, inactivity time for the user to be considered stray
#define USER_GRACE_TIME 3

// In seconds, time the server will wait between strays checks
#define PING_GRACE_TIME 2

// Macro used to store the result of a query
#define store_result_wrapper(result)                                                               \
  result = mysql_store_result(conn);                                                               \
  mysql_next_result(conn);

// Entity types for categorizing participants and elements in the system
typedef enum { ENTITY_TAXI, ENTITY_CUSTOMER, ENTITY_LOCATION } ENTITY_TYPE;

// Possible statuses for each user type.
typedef enum {
  STATUS_TAXI_MOVING,
  STATUS_TAXI_STOPPED,
  STATUS_TAXI_DISCONNECTED,
  STATUS_TAXI_CANT_MOVE,

  STATUS_CUSTOMER_WAITING_TAXI,
  STATUS_CUSTOMER_IN_TAXI,
  STATUS_CUSTOMER_IN_QUEUE,
  STATUS_CUSTOMER_OTHER
} USER_STATUS;

// Enum used to define the purpose of the messages exchanged between the
// different components of the system
typedef enum {
  // Used by the central and digital engine to communicate their respective threads and processes
  // with the separated process that handles the ncurses GUI
  PGUI_WRITE_TOP_WINDOW,
  PGUI_WRITE_BOTTOM_WINDOW,
  PGUI_END_EXECUTION,
  PGUI_TAXI_FATAL_ERROR,
  PGUI_REGISTER_PROCESS,
  PGUI_UPDATE_INFO,

  // Emited by the process that handles ncurses interface to the central
  ORDER_GOTO,
  ORDER_STOP,
  ORDER_CONTINUE,

  // Emitted by a thread of the central that checks continuously if there are
  // any customers or taxis that haven't sent any ping
  STRAY_CUSTOMER,
  STRAY_TAXI,

  // Emitted by their respective user types to indicate that they are active
  PING_CUSTOMER,
  PING_TAXI,

  // Emitted by the different components of the system to inform
  // the central of any event that may affect the system
  REQUEST_NEW_TAXI,
  REQUEST_NEW_CUSTOMER,
  REQUEST_TAXI_RECONNECT,
  REQUEST_DESTINATION_REACHED,
  REQUEST_ASK_FOR_SERVICE,
  REQUEST_TAXI_MOVE,
  REQUEST_TAXI_CANT_MOVE,
  REQUEST_TAXI_CAN_MOVE,
  REQUEST_TAXI_CANT_MOVE_REMINDER,
  REQUEST_TAXI_FATAL_ERROR,
  REQUEST_DISCONNECT_TAXI,
  REQUEST_DISCONNECT_CUSTOMER,

  // Emitted by the central as a response to a request.
  // The prefix indicates the adressee of the message:
  // C-Customer, T-Taxi, M-Map (directed to the GUI handlers)
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
  TRESPONSE_SERVICE_COMPLETED,
  TRESPONSE_START_SERVICE,

  MRESPONSE_MAP_UPDATE,
} SUBJECT;

// Defines the importance of the inconvenience detected by a sensor
typedef enum { IMP_MINOR, IMP_NORMAL, IMP_MAJOR, IMP_FATAL } IMPORTANCE;

// Position in the map
typedef struct {
  int x, y;
} Coordinate;

// Represents an entity relevant for the progress of the system
typedef struct {
  ENTITY_TYPE type;      // Customer, taxi or location
  USER_STATUS status;    // Unset if the entity is a location
  Coordinate coord;      // Position in the map
  int id;                // Represents a char if the entity is not a taxi
  char obj;              // Customer's destination or taxi's service
  bool carryingCustomer; // Only for taxis
} Entity;

// Represents a socket address
typedef struct {
  char ip[20];
  int port;
} Address;

// Represents a message sent by a user to the central
typedef struct {
  SUBJECT subject;           // Purpose of the message
  Coordinate coord;          // Position of the author (may be unused)
  int id;                    // Identification of the author
  char data[UUID_LENGTH];    // Extra data, depending on the subject
  char session[UUID_LENGTH]; // Session id of the system, restarted each time the system restarts.
                             // Its possition as last in the struct is relevant, don't change it
} Request;

// Represents a message sent by the central to a user
typedef struct {
  SUBJECT subject;           // Purpose of the message
  int map[MAP_SIZE];         // 100 (taxis) + 30 (customers) + 30 (locations)
                             // Represents the state of the map
  char id;                   // Identification of the addressee
  char data[UUID_LENGTH];    // Extra data, depending on the subject
  char session[UUID_LENGTH]; // Session id of the system, restarted each time the system restarts.
                             // Its possition as last in the struct is relevant, don't change it
} Response;

// Function used to handle the logging of the components if ncurses is not being used
void log_handler(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message,
                 gpointer user_data);

/// @brief Configures and opens a socket ready to accept connections
///
/// @param port Port to be used
/// @return int Socket descriptor
int openSocket(int port);

/// @brief Connects to a server's socket
///
/// @param server Address of the server
/// @return int Socket descriptor
int connectToServer(Address *server);

/// @brief Generates a unique id
///
/// @param id Unique id, 37 bytes long (including the null terminator)
void generate_unique_id(char id[UUID_LENGTH]);

/// @brief Creates a Kafka Consumer or Producer
///
/// @param server Address of the kafka server
/// @param type Type of the user (consumer or producer)
/// @param id Unique id of the user
/// @return rd_kafka_t* Kafka user
rd_kafka_t *createKafkaUser(Address *server, rd_kafka_type_t type, char *id);

/// @brief Subscribes a kafka consumer to a list of topics
///
/// @param consumer Kafka consumer
/// @param topics List of topics to subscribe to
/// @param topicsCount Number of topics in the list
void subscribeToTopics(rd_kafka_t **consumer, const char **topics, int topicsCount);

/// @brief Sends an event to a kafka topic
///
/// @param producer Kafka producer that will send the event
/// @param topic Topic to send the event to
/// @param value Value to send
/// @param valueSize Size of the value
void sendEvent(rd_kafka_t *producer, const char *topic, void *value, size_t valueSize);

/// @brief Polls a message discarding the ones that are older than the determined time
///
/// @param rk Kafka consumer
/// @param timeout_ms Timeout in milliseconds
/// @return rd_kafka_message_t* Message or NULL if there is no message or it's too old
rd_kafka_message_t *poll_wrapper(rd_kafka_t *rk, int timeout_ms);

/// @brief Serializes an entity into an int. It takes advantage of the fact that not all the size of
/// the fields is used (e.g. Coordinates values should be in the range [0, 19]). If these
/// presuppositions change, this function will need to be updated
///
/// @param entity Entity to be serialized
/// @return int Serialized entity
int serializeEntity(Entity *entity);

/// @brief Deserializes an entity from an int. It takes advantage of the fact that not all the size
/// of the fields is used (e.g. Coordinates values should be in the range [0, 19]). If these
/// presuppositions change, this function will need to be updated
///
/// @param dest Entity where the deserialized entity will be stored
/// @param entity Serialized entity
void deserializeEntity(Entity *dest, int entity);

#endif