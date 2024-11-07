#ifndef KAFKA_MODULE_H
#define KAFKA_MODULE_H

#include "common.h"
#include <stdbool.h>

/// @brief Entry point of the kafka module
///
/// This module handles the communications with the kafka server.
/// As these are most of the communications, this is the core of the central and the module which
/// handles the majority of the database operations.
void startKafkaServer();

/// @brief Loads the map from the database
void loadMap();

/// @brief Initializes the kafka module
///
/// This includes initializing the kafka consumer and producer, as well as the database connection
/// and the necessary variables to control the flux of the different threads.
void init();

/// @brief Disposes any resource that needs to be disposed.
/// Intended to be called when the program is exiting.
void cleanUp();

/// @brief Stores the movement of a taxi in the database
///
/// @param request Request containing the necessary information to perform the movement
void moveTaxi(Request *request);

/// @brief Introduces a new customer into the system
///
/// @param request Request containing the necessary information to perform the movement
void insertCustomer(Request *request);

/// @brief Processes a service request, assigning a taxi to the customer if possible and
/// communicating the result with him.
///
/// @param request Request containing the necessary information to perform the movement
void processServiceRequest(Request *request);

/// @brief Informs a taxi its last position before disconnecting
///
/// @param request Request containing the necessary information to perform the movement
void resumePosition(Request *request);

/// @brief Informs the taxi where to go to, if possible
///
/// @param request Request containing the necessary information to perform the movement
/// @param reconnected Whether the cause of the call is a reconnection. It only affects the
/// logging
void refreshTaxiInstructions(Request *request, bool reconnected);

/// @brief Informs the customer that it has been picked up by the taxi and refreshes the taxi
/// instructions
///
/// @param request Request containing the necessary information to perform the movement
void pickUpCustomer(Request *request);

/// @brief Informs the customer that the service has been completed and checks if there's any
/// service in the queue for the now available taxi to take care of
///
/// @param request Request containing the necessary information to perform the movement
void completeService(Request *request);

/// @brief Checks if there are any customers in the queue. If so, it assigns a taxi to the client
/// that has been longer in the queue, if possible
void checkQueue();

/// @brief Sends an order to a taxi. These are the orders that the user has selected through the
/// ncurses GUI
///
/// @param request Request containing the necessary information to perform the movement
void sendOrder(Request *request);

/// @brief Tries to assign a taxi to a customer. If it succeeds, it returns true, otherwise false
///
/// @param customerId Customer to be assigned
/// @param customerCoord Coordinate of the customer
/// @return true A taxi has been assigned to the customer
/// @return false There aren't any taxis available
bool assignTaxi(char customerId, Coordinate customerCoord);

/// @brief Disconnects a customer from the system
///
/// @param request Request containing the necessary information to perform the movement
void disconnectCustomer(Request *request);

/// @brief Disconnects a taxi from the system. Reassigning its client (if any) to another taxi or
/// enqueueing it at the highest priority
///
/// @param request Request containing the necessary information to perform the movement
void disconnectTaxi(Request *request);

/// @brief Stores whether a taxi can move or not in the database
///
/// @param taxiId Taxi to be updated
/// @param canMove Whether the taxi can move or not
void setTaxiCanMove(int taxiId, bool canMove);

/// @brief Updates a taxi or customer's last update time in the database
///
/// @param request Request containing the necessary information to perform the movement
void refreshLastUpdate(Request *request);

/// @brief Function intended to be executed by a separate thread or process. Continuously checks
/// for strays (taxi or customers that haven't shown signs of life for a while) and sends a kafka
/// message to the central to take action
///
/// @return void* Returns NULL always. It just exists to fill the required signature for a thread
/// intended function
void *checkStrays();

#endif