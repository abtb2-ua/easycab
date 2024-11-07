#include "common.h"
#include "glib.h"
#include "ncurses_common.h"
#include <bits/pthreadtypes.h>
#include <bits/time.h>
#include <ctype.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MINOR_WAIT_TIME 5
#define NORMAL_WAIT_TIME 15
#define MAJOR_WAIT_TIME 30
#define FATAL_WAIT_TIME 999

Address taxi;
int serverSocket;
int taxiStopped;
int seconds = 0;
bool fatal = false;
pthread_mutex_t mut;
bool stopProgram;
// These are sent to the taxi always, even if no option has been selected
// Thus, need to be given a default value
IMPORTANCE importance = IMP_MINOR;
int reason = 0;
// In seconds, time given to the taxi to send a message. If no message
// is received, the taxi is considered disconnected
const int COURTESY_TIME = 1500;
const int MENU_WIDTH = 28;
const int MENU_HEIGHT = 8;
const int POPUP_WIDTH = 60;
// -4 for borders and margins and +1 for '\0'
#define LINE_LENGTH POPUP_WIDTH - 3

#define OPTIONS_MARGIN 5

#define dispose(win)                                                                               \
  if (win) {                                                                                       \
    werase(win);                                                                                   \
    wrefresh(win);                                                                                 \
    delwin(win);                                                                                   \
    win = NULL;                                                                                    \
  }

/// @brief Parses the arguments passed to the program
///
/// @param argc number of arguments
/// @param argv array of arguments
void checkArguments(int argc, char *argv[]);

/// @brief Intended to be executed by a separate thread or process. Acts as a server to the taxi.
/// Handles the communication with it until the program ends. Ends the program when the
/// communication with the taxi is lost
///
/// @return void* Returns NULL always. It just exists to fill the required signature for a thread
/// intended function
void *communicateWithTaxi();

/// @brief Handles the input from the user in form of an ncurses based menu
void menu();

/// @brief Prints the menu in the ncurses window
///
/// @param win Window in which the menu will be printed
/// @param selectedOption Option selected by the user
void printMenu(WINDOW *win, int selectedOption);

/// @brief Gets the value of the stop flag locking and unlocking the respective mutex. Used merely
/// for readability purposes
///
/// @return bool Value of the stop flag
bool getStop();

int main(int argc, char *argv[]) {
  srand(time(NULL));

  char buffer[BUFFER_SIZE];

  g_log_set_default_handler(log_handler, NULL);
  checkArguments(argc, argv);
  serverSocket = connectToServer(&taxi);

  g_message("Connected to taxi");

  buffer[0] = ENQ;
  write(serverSocket, buffer, BUFFER_SIZE);

  if (read(serverSocket, buffer, BUFFER_SIZE) <= 0) {
    g_error("Error reading from socket");
  }

  if (buffer[0] != ACK) {
    g_error("Couldn't connect to taxi");
  }

  pthread_t thread;
  pthread_create(&thread, NULL, communicateWithTaxi, NULL);
  pthread_detach(thread);
  menu();
}

void checkArguments(int argc, char *argv[]) {
  char usage[100];

  sprintf(usage, "Usage: %s <Taxi IP:port>", argv[0]);

  if (argc < 2)
    g_error("%s", usage);

  if (sscanf(argv[1], "%[^:]:%d", taxi.ip, &taxi.port) != 2)
    g_error("Invalid taxi address. %s", usage);

  if (taxi.port < 1 || taxi.port > 65535)
    g_error("Invalid taxi port, must be between 0 and 65535. %s", usage);
}

void *communicateWithTaxi() {
  char buffer[BUFFER_SIZE];
  fd_set readfds;
  struct timespec time;
  int precission = 5 * 1000;
  clock_gettime(CLOCK_MONOTONIC, &time);
  time_t last = time.tv_sec;

  while (!getStop()) {
    pthread_mutex_lock(&mut);
    buffer[0] = STX;
    buffer[1] = (int)(seconds == 0);
    buffer[2] = fatal;
    pthread_mutex_unlock(&mut);
    memcpy(buffer + 3, &importance, sizeof(IMPORTANCE));
    memcpy(buffer + 3 + sizeof(IMPORTANCE), &reason, sizeof(int));

    write(serverSocket, buffer, BUFFER_SIZE);

    FD_ZERO(&readfds);
    FD_SET(serverSocket, &readfds);
    if (select(serverSocket + 1, &readfds, NULL, NULL,
               &(struct timeval){0, COURTESY_TIME * 1000}) <= 0)
      break;

    if (read(serverSocket, buffer, BUFFER_SIZE) <= 0 || buffer[0] != STX)
      break;

    pthread_mutex_lock(&mut);
    if (seconds != 0)
      seconds--;
    taxiStopped = buffer[1];
    pthread_mutex_unlock(&mut);
    // g_message("Move state: %i", moveState);
    // sleep(1);
    while (time.tv_sec == last) {
      clock_gettime(CLOCK_MONOTONIC, &time);
      usleep(precission);
    }
    last = time.tv_sec;
  }

  pthread_mutex_lock(&mut);
  stopProgram = true;
  pthread_mutex_unlock(&mut);

  return NULL;
}

void menu() {
  initscr();
  raw();
  start_color_wrapper();
  noecho();
  curs_set(0);

  int maxx, maxy;
  int selectedOption = 0;
  int selectedFatalOption = 0;
  bool localTaxiStopped;
  bool pop_up_shown = true; // true: taxi stopped, false: inconvenience

  getmaxyx(stdscr, maxy, maxx);

  WINDOW *menu_win =
      newwin(MENU_HEIGHT, MENU_WIDTH, (maxy - MENU_HEIGHT) / 2, (maxx - MENU_WIDTH) / 2);
  WINDOW *pop_up = NULL;
  WINDOW *clock = NULL;
  keypad(menu_win, TRUE);
  wtimeout(menu_win, 500);

  attron(A_DIM);
  printw("(Press 'q' to exit)");
  attroff(A_DIM);
  refresh();

  printMenu(menu_win, selectedOption);

  while (!getStop()) {
    pthread_mutex_lock(&mut);
    localTaxiStopped = taxiStopped;
    pthread_mutex_unlock(&mut);
    if (seconds == 0 && !localTaxiStopped) {
      dispose(pop_up);
      dispose(clock);

      printMenu(menu_win, selectedOption);

      int c = wgetch(menu_win);
      if (tolower(c) == 'q' || c == ctrl('c') || c == ctrl('z')) {
        break;
      } else if (c == KEY_DOWN) {
        selectedOption = (selectedOption + 1) % 5;
      } else if (c == KEY_UP) {
        selectedOption = (selectedOption + 4) % 5;
      } else if (c == '\n') {
        pthread_mutex_lock(&mut);
        if (selectedOption == 0) {
          seconds = MINOR_WAIT_TIME;
          importance = IMP_MINOR;
        } else if (selectedOption == 1) {
          seconds = NORMAL_WAIT_TIME;
          importance = IMP_NORMAL;
        } else if (selectedOption == 2) {
          seconds = MAJOR_WAIT_TIME;
          importance = IMP_MAJOR;
        } else if (selectedOption == 3) {
          seconds = FATAL_WAIT_TIME;
          importance = IMP_FATAL;
        } else {
          pthread_mutex_unlock(&mut);
          break;
        }
        reason = rand() % INCONVENIENCES_COUNT;
        pthread_mutex_unlock(&mut);

        wattron(menu_win, A_DIM);
        printMenu(menu_win, selectedOption);
        wattroff(menu_win, A_DIM);
      }
    } else {
      char lines[MAX_LINES][LINE_LENGTH];
      int height, margin, numLines;

      if (seconds != 0) {
        if (pop_up_shown) {
          pop_up_shown = false;
          dispose(pop_up);
          dispose(clock);
        }

        numLines = split(lines, inconveniences[importance][reason], LINE_LENGTH);
        height = 4 + numLines + numLines % 2; // make it even
        margin = 1;
      } else {
        if (!pop_up_shown) {
          pop_up_shown = true;
          dispose(pop_up);
          dispose(clock);
        }

        strcpy(lines[0], "Taxi is not moving.");
        strcpy(lines[1], "Please wait...");
        numLines = 2;
        height = 6;
        margin = 2;
      }

      if (!pop_up) {
        pop_up = newwin(height, POPUP_WIDTH, (maxy - height) / 2, (maxx - POPUP_WIDTH) / 2);

        wattron(menu_win, A_DIM);
        printMenu(menu_win, selectedOption);
        wattroff(menu_win, A_DIM);
        for (int i = 0; i < numLines; i++) {
          mvwprintw(pop_up, i + margin, (POPUP_WIDTH - strlen(lines[i])) / 2, "%s\n", lines[i]);
        }
        box(pop_up, 0, 0);
        wrefresh(pop_up);
      }

      if (!clock) {
        clock = newwin(1, POPUP_WIDTH - 2, (maxy - height) / 2 + height - 2,
                       (maxx - POPUP_WIDTH + 2) / 2);
      }

      // wclear(clock);
      if (seconds != 0) {
        if (seconds > MAJOR_WAIT_TIME) {
          seconds = FATAL_WAIT_TIME;
          char *options[3] = {
              "[  RECOVER  ]",
              "[ TERMINATE ]",
              "[   EXIT    ]",
          };
          char margin[OPTIONS_MARGIN];
          for (int i = 0; i < OPTIONS_MARGIN; i++)
            margin[i] = ' ';
          int len =
              strlen(options[0]) + strlen(options[1]) + strlen(options[2]) + 2 * OPTIONS_MARGIN;
          wmove(clock, 0, (POPUP_WIDTH - 2 - len) / 2);
          for (int i = 0; i < 3; i++) {
            if (i == selectedFatalOption)
              wattron(clock, A_REVERSE);
            waddstr(clock, options[i]);
            wattroff(clock, A_REVERSE);
            if (i < 2)
              waddstr(clock, margin);
          }
        } else {
          mvwprintw(clock, 0, (POPUP_WIDTH - 2 - 10) / 2, "%2.i seconds", seconds);
        }
        wrefresh(clock);
      }

      int c = wgetch(menu_win);
      if (tolower(c) == 'q') {
        break;
      } else if (seconds > MAJOR_WAIT_TIME) {
        if (c == KEY_LEFT) {
          selectedFatalOption = (selectedFatalOption + 2) % 3;
        } else if (c == KEY_RIGHT) {
          selectedFatalOption = (selectedFatalOption + 1) % 3;
        } else if (c == ' ' || c == '\n') {
          if (selectedFatalOption == 0) {
            pthread_mutex_lock(&mut);
            seconds = 0;
            pthread_mutex_unlock(&mut);
          } else if (selectedFatalOption == 1) {
            pthread_mutex_lock(&mut);
            fatal = true;
            pthread_mutex_unlock(&mut);
            dispose(pop_up) dispose(clock) dispose(menu_win);
            erase();
            char *msg = "Terminating...";
            WINDOW *terminate = newwin(1, strlen(msg), (maxy - 1) / 2, (maxx - strlen(msg)) / 2);
            wattron(terminate, COLOR_PAIR(PASTEL_RED));
            waddstr(terminate, msg);
            wrefresh(terminate);
            usleep(1.5 * 1000 * 1000);
            dispose(terminate);
            break;
          } else if (selectedFatalOption == 2) {
            break;
          }
        }
      }
    }
  }

  bool error = false;

  pthread_mutex_lock(&mut);
  if (stopProgram) // Hasn't been stopped by the user
    error = true;
  pthread_mutex_unlock(&mut);

  dispose(pop_up);
  dispose(clock);
  dispose(menu_win);

  char *msg = "Goodbye!";
  char *msg2 = "(Press any key to exit...)";
  char *errmsg = "Error! Lost connection with taxi!";

  // mvprintw(0, 0, "                         ");
  erase();
  if (error) {
    init_pair(1, PASTEL_RED, COLOR_BLACK);
    attron(COLOR_PAIR(1));
    mvprintw(maxy / 2 - 1, maxx / 2 - strlen(errmsg) / 2, "%s", errmsg);
    attroff(COLOR_PAIR(1));
  } else {
    mvprintw(maxy / 2 - 1, maxx / 2 - strlen(msg) / 2, "%s", msg);
  }
  attron(A_DIM);
  mvprintw(maxy / 2 + 1, maxx / 2 - strlen(msg2) / 2, "%s", msg2);

  refresh();
  getch();
  endwin();
}

void printMenu(WINDOW *win, int selectedOption) {
  box(win, 0, 0);
  wattron(win, A_BOLD);
  mvwprintw(win, 1, 1, "Options");
  wattroff(win, A_BOLD);
  mvwprintw(win, 2, 1, "[%s] Minor inconvenience", selectedOption == 0 ? ">" : " ");
  mvwprintw(win, 3, 1, "[%s] Inconvenience", selectedOption == 1 ? ">" : " ");
  mvwprintw(win, 4, 1, "[%s] Major inconvenience", selectedOption == 2 ? ">" : " ");
  mvwprintw(win, 5, 1, "[%s] Fatal error", selectedOption == 3 ? ">" : " ");
  mvwprintw(win, 6, 1, "[%s] Exit", selectedOption == 4 ? ">" : " ");
  wrefresh(win);
}

bool getStop() {
  pthread_mutex_lock(&mut);
  bool b = stopProgram;
  pthread_mutex_unlock(&mut);
  return b;
}
