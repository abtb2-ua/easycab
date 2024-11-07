#ifndef SOCKET_MODULE_H
#define SOCKET_MODULE_H

#include <mysql/mysql.h>
#include <stdbool.h>

/// @brief Entry point of the socket module
///
/// This module handles the authentications of the digital engines. It listens for petitions from
/// the determined port and handles the petitions accordingly.
///
/// @param listenPort Port to be used to listen for petitions
void listenSocket(int listenPort);

/// @brief Function intended to be executed by a separate thread or process. Attends a petition of
/// authentication from a digital engine and communicates the result (if successful) to the central
///
/// @return void* Returns NULL always. It just exists to fill the required signature for a thread
/// intended function
void *attend(void *args);

/// @brief Checks whether an id proposed by a digital engine is valid or not
///
/// @param conn Database connection
/// @param id Id proposed by the digital engine
/// @param reconnected Whether the digital engine is reconnecting. It's an ouptut parameter
/// @return true The id proposed is valid
/// @return false The id proposed is not valid
bool checkId(MYSQL *conn, int id, bool *reconnected);

#endif