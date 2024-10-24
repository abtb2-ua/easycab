#ifndef KAFKA_MODULE_H
#define KAFKA_MODULE_H

#include "common.h"
#include <stdbool.h>

void startKafkaServer();
void loadMap();
void initConnection();
void init();
void cleanUp();
void moveTaxi(Request *request);
void insertClient(Request *request);
void processServiceRequest(Request *request);
void pickUpClient(Request *request);
void completeService(Request *request);
bool assignTaxi(char clientId, Coordinate clientCoord);

#endif