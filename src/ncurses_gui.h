#ifndef NCURSES_GUI_H
#define NCURSES_GUI_H

#include "common.h"

#define WIDTH 30
#define HEIGHT 12

void ncursesGui(int p);
void *readInput();
void registerProcess(int p);
void ncurses_log_handler(const gchar *log_domain, GLogLevelFlags log_level,
                         const gchar *message, gpointer user_data);

#endif