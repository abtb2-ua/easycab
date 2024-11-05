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
void insertCustomer(Request *request);
void processServiceRequest(Request *request);
void resumePosition(Request *request);
void refreshTaxiInstructions(Request *request, bool reconnected);
void pickUpCustomer(Request *request);
void completeService(Request *request);
void checkQueue();
void sendOrder(Request *request);
bool assignTaxi(char clientId, Coordinate clientCoord);
void disconnectCustomer(Request *request);
void disconnectTaxi(Request *request);
void setTaxiCanMove(int taxiId, bool canMove);
void refreshLastUpdate(Request *request);
void *checkStrays();

#endif