#ifndef EC_DE_NCURSES_GUI_H
#define EC_DE_NCURSES_GUI_H

#include "common.h"
#include <glib.h>
#include <stdbool.h>

/// @brief Entry point of the ncurses GUI.
///
/// This module handles the ncurses GUI of the digital engine. Offers the user the possibility to
/// visualize the logs or a table-like interface with all the information relevant about the taxi
///
/// @param p End of a pipe used to receive the necessary information from the digital engine
void ncursesGui(int p);

/// @brief Handles the logging of the ncurses GUI through the glib API
void ncurses_log_handler(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message,
                         gpointer user_data);

/// @brief Print the stored logs of the digital engine
void printLogs();

/// @brief Prints a log message sent from the central (if there's any message to be printed)
///
/// @param p Pipe used to receive the necessary information from the central
/// @return true The program should continue
/// @return false Central has requested the program to exit
bool handleInput(int c);

/// @brief Updates the content of the menu based on the user's previous input
void printMenu();

/// @brief Prints in form of tables the state of the taxi
void printTableView();

/// @brief Translates the received information from the digital engine into strings to be printed in
/// the table view
///
/// @param buffer Information received from the digital engine
void parseRows(char buffer[BUFFER_SIZE]);

#endif