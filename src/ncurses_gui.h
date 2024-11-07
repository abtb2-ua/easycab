#ifndef NCURSES_GUI_H
#define NCURSES_GUI_H

#include <glib.h>
#include <librdkafka/rdkafka.h>
#include <stdbool.h>

// Represents the width of the vertical window to the left used as menu
#define WIDTH 30
// Represents the height of the horizontal window used to the bottom-right
#define HEIGHT 12

/// @brief Entry point of the ncurses GUI.
///
/// This module handles the ncurses GUI of the central. Shows in an orderly manner the output of the
/// central while also offering the user the possibility to interact with the system through a close
/// set of actions (e.g. stopping a taxi, deciding where it should go now, etc.).
///
/// @param p End of a pipe used to receive the necessary information from the central
void ncursesGui(int p);

/// @brief Registers a process to be killed when the program exits
///
/// @param process Descriptor of the process to be registered
void registerProcess(int process);

/// @brief Prints a log message sent from the central (if there's any message to be printed)
///
/// @param p Pipe used to receive the necessary information from the central
/// @return true The program should continue
/// @return false Central has requested the program to exit
bool printPetition(int p);

/// @brief Updates the content of the menu based on the user's previous input
void printMenu();

/// @brief Prints the stored logs of the central
void printLogs();

/// @brief Prints in form of tables the state of the system
void printTableView();

/// @brief Handles the input of the user
///
/// @param c Character received by the ncurses GUI
void handleInput(int c);

/// @brief Intended to be executed by a separate thread or process. Reads and updates a local copy
/// of the map from the kafka server for the other threads to use
///
/// @return void* Returns NULL always. It just exists to fill the required signature for a thread
/// intended function
void *readMap();

#endif