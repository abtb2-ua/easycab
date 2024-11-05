#ifndef NCURSES_GUI_H
#define NCURSES_GUI_H

#define WIDTH 30
#define HEIGHT 12

#include "common.h"
#include <glib.h>
#include <librdkafka/rdkafka.h>

void ncursesGui(int p);
void *readInput();
void registerProcess(int p);
void ncurses_log_handler(const gchar *log_domain, GLogLevelFlags log_level,
                         const gchar *message, gpointer user_data);
int printPetition(int p);
void printMenu();
void printLogs();
void printTableView();
void handleInput(int c);
void *readMap();

#endif