#ifndef COMMON_H
#define COMMON_H

#include <iostream>

#define BUFFER_SIZE 300
#define GRID_SIZE 20
#define MAP_SIZE 160

enum class AGENT_TYPE { TAXI = 0, CLIENT, LOCATION };
enum class AGENT_STATUS {
  TAXI_EMPTY = 0,
  TAXI_CARRYING_CLIENT,
  TAXI_STOPPED,
  TAXI_DISCONNECTED,
  CLIENT_WAITING_TAXI,
  CLIENT_IN_TAXI,
  CLIENT_IN_QUEUE,
  CLIENT_OTHER
};

enum class SUBJECT {
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
};

typedef struct Coordinate {
  int x, y;
} Coordinate;

class Agent {
public:
  AGENT_TYPE type;
  AGENT_STATUS status;
  Coordinate coord;
  int id;
  char obj; // Customer's destination or taxi's service
  bool carryingCustomer;
  bool canMove;

  void deserialize(int src);
};

class Address {
public:
  std::string ip;
  int port;

  Address();

  void parse(std::string str);
};

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

#endif