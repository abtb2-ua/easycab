#include "ncurses_common.h"

void init_color_rgb(int color, int r, int g, int b) {
  init_color(color, r * 1000 / 256, g * 1000 / 256, b * 1000 / 256);
}

void start_color_wrapper() {
  start_color();

  init_color(COLOR_BLACK, 75, 75, 75);

  init_color_rgb(PASTEL_RED, 255, 105, 97);
  init_color_rgb(PASTEL_GREEN, 119, 221, 119);
  init_color_rgb(PASTEL_YELLOW, 253, 253, 150);
  init_color_rgb(PASTEL_BLUE, 132, 182, 244);
  init_color_rgb(PASTEL_MAGENTA, 253, 202, 225);
  init_color_rgb(PASTEL_PURPLE, 216, 132, 244);
  init_color_rgb(PASTEL_ORANGE, 255, 189, 102);
  init_pair(PASTEL_RED, PASTEL_RED, COLOR_BLACK);
  init_pair(PASTEL_GREEN, PASTEL_GREEN, COLOR_BLACK);
  init_pair(PASTEL_YELLOW, PASTEL_YELLOW, COLOR_BLACK);
  init_pair(PASTEL_BLUE, PASTEL_BLUE, COLOR_BLACK);
  init_pair(PASTEL_MAGENTA, PASTEL_MAGENTA, COLOR_BLACK);
  init_pair(PASTEL_PURPLE, PASTEL_PURPLE, COLOR_BLACK);
  init_pair(PASTEL_ORANGE, PASTEL_ORANGE, COLOR_BLACK);
}