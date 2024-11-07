#include "ncurses_common.h"
#include "common.h"
#include "glib.h"
#include <ncurses.h>
#include <string.h>

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

int find(const char text[], char character) {
  int i;
  for (i = 0; i < strlen(text); i++) {
    if (text[i] == character)
      return i;
  }
  return -1;
}

int split(void *dest, const char text[], int lineLength) {
  char lines[MAX_LINES][lineLength];
  char word[20];
  int p = 0;
  int p2 = 0;
  int p3 = 0;
  int currentLine = 0;
  lines[0][0] = '\0';
  while (p < strlen(text)) {
    p2 = find(text + p, ' ');
    if (p2 == -1) {
      p2 = strlen(text + p);
    }
    strncpy(word, text + p, p2);
    word[p2] = '\0';
    // +2 for ' ' and '\0'
    if (strlen(lines[currentLine]) + p2 + 2 > lineLength) {
      currentLine++;
      lines[currentLine][0] = '\0';
      p3 = 0;
    }
    sprintf(lines[currentLine] + p3, "%s ", word);
    p = p + p2 + 1;
    p3 = p3 + p2 + 1;
  }
  lines[currentLine][p3 + strlen(word) + 1] = '\0';

  memcpy(dest, lines, sizeof(lines));
  return currentLine + 1;
}

void printFinishPopUp(char *extra) {
  char *msg1 = "Exiting...";
  char *msg2 = "(Press any key to continue)";
  int maxx = getmaxx(stdscr);
  int maxy = getmaxy(stdscr);
  int pop_up_width = strlen(msg2) + 8;

  WINDOW *pop_up = newwin(6, pop_up_width, (maxy - 5) / 2, (maxx - strlen(msg2) - 4) / 2);

  box(pop_up, 0, 0);
  wattron(pop_up, COLOR_PAIR(PASTEL_RED));
  // Third msg exists to balance the first space in case that extra is not printed
  // This way the message will always be centered
  mvwprintw(pop_up, 2, (pop_up_width - strlen(msg1)) / 2, "%s %s%s", extra ? extra : "", msg1,
            extra ? "" : " ");
  wattroff(pop_up, COLOR_PAIR(PASTEL_RED));
  wattron(pop_up, A_DIM);
  mvwaddstr(pop_up, 3, (pop_up_width - strlen(msg2)) / 2, msg2);
  wrefresh(pop_up);

  getchar();
  endwin();
  printf("Exiting...\n");
}

void ncurses_log_handler(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message,
                         gpointer user_data) {
  if ((log_level & G_LOG_LEVEL_MASK) == G_LOG_LEVEL_DEBUG) {
    char *debug_env = getenv("G_MESSAGES_DEBUG");

    if (debug_env == NULL)
      return;
  }

  int screen = *(int *)user_data;
  int p = *(int *)(user_data + sizeof(int));

  char buffer[BUFFER_SIZE];

  struct timeval now;
  gettimeofday(&now, NULL);
  int hours = (now.tv_sec / 3600) % 24 + 2; // UTC + 2
  int minutes = (now.tv_sec / 60) % 60;
  int seconds = now.tv_sec % 60;
  int milliseconds = now.tv_usec / 1000.0;

  buffer[0] = screen;
  buffer[1] = PASTEL_BLUE;
  sprintf(buffer + 2, "[%i:%02i:%02i.%03i] ", hours, minutes, seconds, milliseconds);
  write(p, buffer, BUFFER_SIZE);

  switch (log_level & G_LOG_LEVEL_MASK) {
  case G_LOG_LEVEL_CRITICAL:
    buffer[1] = PASTEL_PURPLE;
    strcpy(buffer + 2, "** CRITICAL **");
    break;
  case G_LOG_LEVEL_WARNING:
    buffer[1] = PASTEL_ORANGE;
    strcpy(buffer + 2, "** WARNING **");
    break;
  case G_LOG_LEVEL_MESSAGE:
    buffer[1] = PASTEL_GREEN;
    strcpy(buffer + 2, "Message");
    break;
  case G_LOG_LEVEL_DEBUG:
    buffer[1] = PASTEL_MAGENTA;
    strcpy(buffer + 2, "Debug");
    break;
  case G_LOG_LEVEL_INFO:
    buffer[1] = PASTEL_BLUE;
    strcpy(buffer + 2, "Info");
    break;
  case G_LOG_LEVEL_ERROR:
    buffer[1] = PASTEL_RED;
    strcpy(buffer + 2, "** ERROR **");
    break;
  default:
    break;
  }

  write(p, buffer, BUFFER_SIZE);

  sprintf(buffer + 2, ": %s\n", message);
  buffer[1] = 0;
  write(p, buffer, BUFFER_SIZE);

  if ((log_level & G_LOG_LEVEL_MASK) == G_LOG_LEVEL_ERROR) {
    buffer[0] = PGUI_END_EXECUTION;
    write(p, buffer, BUFFER_SIZE);
    return;
  }
}