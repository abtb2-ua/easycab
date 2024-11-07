#ifndef NCURSES_COMMON_H
#define NCURSES_COMMON_H

#include <glib.h>
#include <ncurses.h>

// Represents a key combination (ctrl + x)
#define ctrl(x) ((x) & 0x1f)

#define MAX_LINES 10

// Colors used in the GUI. The index is started at 100 to avoid conflicts with the existent terminal
// colors
enum COLORS {
  PASTEL_RED = 100,
  PASTEL_GREEN,
  PASTEL_YELLOW,
  PASTEL_BLUE,
  PASTEL_MAGENTA,
  PASTEL_PURPLE,
  PASTEL_ORANGE
};

/// @brief Initializes a 256-based rgb color in the ncurses palette
///
/// @param color Color to be initialized
/// @param r Red component
/// @param g Green component
/// @param b Blue component
void init_color_rgb(int color, int r, int g, int b);

/// @brief Prepares the ncurses GUI for the use of colors (e.g. initializing the palette)
void start_color_wrapper();

/// @brief Handles the logging of the ncurses GUI through the glib API
void ncurses_log_handler(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message,
                         gpointer user_data);

/// @brief Prints a pop-up message informing the user that the program is exiting
///
/// @param extra Extra message to be printed, if NULL, no more than the default message will be
/// printed
void printFinishPopUp(char *extra);

/// @brief Finds a character in a string
///
/// @param text String to be searched
/// @param character Character to be found
/// @return int Position of the character in the string or -1 if not found
int find(const char text[], char character);

/// @brief Splits a string into lines of a given length without breaking words
///
/// @param dest Array of strings where the lines will be stored
/// @param text String to be split
/// @param lineLength Maximum length of each line
/// @return int Number of lines
int split(void *dest, const char text[], int lineLength);

#endif