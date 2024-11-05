#ifndef SOCKET_MODULE_H
#define SOCKET_MODULE_H

#include "common.h"

void listenSocket(int listenPort);
void *attend(void *args);
bool checkId(MYSQL *conn, int id, bool *reconnected);

#endif