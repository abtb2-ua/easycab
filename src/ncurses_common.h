#ifndef NCURSES_COMMON_H
#define NCURSES_COMMON_H

#include <ncurses.h>

#define ctrl(x) ((x) & 0x1f)

enum COLORS {
  PASTEL_RED = 100,
  PASTEL_GREEN,
  PASTEL_YELLOW,
  PASTEL_BLUE,
  PASTEL_MAGENTA,
  PASTEL_PURPLE,
  PASTEL_ORANGE
};

void init_color_rgb(int color, int r, int g, int b);
void start_color_wrapper();

#endif