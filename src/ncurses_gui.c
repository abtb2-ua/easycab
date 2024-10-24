#include "ncurses_gui.h"
#include "common.h"
#include <ncurses.h>
#include <signal.h>
#include <string.h>
#include <time.h>

WINDOW *top_box;
WINDOW *menu_win, *top_win, *bottom_win;
pid_t processes[5];
int processCount = 0;

void init_color_rgb(int color, int r, int g, int b) {
  init_color(color, r * 1000 / 256, g * 1000 / 256, b * 1000 / 256);
}

void finish() {
  for (int i = 0; i < processCount; i++)
    kill(processes[i], SIGKILL);

  wattron(top_win, COLOR_PAIR(PASTEL_RED));
  waddstr(top_win, "\n\nExiting...\n");
  wattroff(top_win, COLOR_PAIR(PASTEL_RED));
  wattron(top_win, A_DIM);
  wprintw(top_win, "(Press any key to continue)");
  wrefresh(top_win);

  getchar();
  endwin();
  printf("Exiting...\n");
  printf("Processes stopped: %i\n", processCount);
}

void ncurses_log_handler(const gchar *log_domain, GLogLevelFlags log_level,
                         const gchar *message, gpointer user_data) {
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
  sprintf(buffer + 2, "[%i:%02i:%02i.%03i] ", hours, minutes, seconds,
          milliseconds);
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

void ncursesInit() {
  initscr();
  curs_set(0);
  noecho();
  start_color();
  cbreak();

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

  int maxx, maxy;

  getmaxyx(stdscr, maxy, maxx);

  menu_win = newwin(maxy, WIDTH, 0, 0);
  top_box = newwin(maxy - HEIGHT, maxx - WIDTH + 1, 0, WIDTH - 1);
  top_win = newwin(maxy - HEIGHT - 2, maxx - WIDTH - 1, 1, WIDTH + 1);
  bottom_win = newwin(HEIGHT, maxx - WIDTH - 1, maxy - HEIGHT, WIDTH + 1);

  keypad(stdscr, TRUE);
  box(menu_win, 0, 0);

  chtype c = ACS_HLINE;
  chtype c2 = ACS_LTEE;
  wborder(top_box, ACS_VLINE, ' ', ' ', c, ACS_BBSS, ' ', c2, c);

  scrollok(menu_win, TRUE);
  scrollok(top_win, TRUE);
  scrollok(bottom_win, TRUE);

  wmove(menu_win, 1, 1);
  wmove(bottom_win, 0, 0);
  wmove(top_win, 0, 0);

  wrefresh(menu_win);
  wrefresh(top_box);

  wtimeout(menu_win, 10);
}

void ncursesGui(int p) {
  ncursesInit();
  fd_set readfds;

  char buffer[BUFFER_SIZE];

  while (true) {
    char c = wgetch(menu_win);
    if (c == ERR) {
      wrefresh(top_win);
    } else {
      if (c == 'f') {
        break;
      }
    }

    WINDOW **win;

    FD_ZERO(&readfds);
    FD_SET(p, &readfds);
    if (select(p + 1, &readfds, NULL, NULL, &(struct timeval){0, 100}) > 0) {
      read(p, buffer, BUFFER_SIZE);

      switch (buffer[0]) {
      case PGUI_WRITE_BOTTOM_WINDOW:
        win = &bottom_win;
        break;

      case PGUI_WRITE_TOP_WINDOW:
        win = &top_win;
        break;

      case PGUI_END_EXECUTION:
        finish();
        return;

      case PGUI_REGISTER_PROCESS:
        memcpy(&processes[processCount], buffer + 1, sizeof(pid_t));
        processCount++;
        continue;

      default:
        continue;
      }

      int color = buffer[1];

      if (color != 0) {
        wattron(*win, COLOR_PAIR(color));
        waddstr(*win, buffer + 2);
        wattroff(*win, COLOR_PAIR(color));
      } else {
        waddstr(*win, buffer + 2);
      }
      wrefresh(*win);
    }
  }

  finish();
}