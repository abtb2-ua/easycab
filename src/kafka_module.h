#ifndef KAFKA_MODULE_H
#define KAFKA_MODULE_H

#include "common.h"
#include <stdbool.h>

void startKafkaServer();
void loadMap();
void initConnection();
void init();
void cleanUp();
void moveTaxi();
bool assignTaxi(char clientId, Coordinate clientCoord);

#endif