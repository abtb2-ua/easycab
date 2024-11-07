#include "EC_DE_ncurses_gui.h"
#include "common.h"
#include "data_structures.h"
#include "ncurses_common.h"
#include <ncurses.h>
#include <signal.h>
#include <sys/select.h>

#define WIDTH 20
#define HEIGHT 6
#define NUM_HEADERS 8

Queue *logs;            // Queue of logs received from the digital engine
WINDOW *menu = NULL;    // Top right window containing the menu
WINDOW *content = NULL; // Left side window containing the table or the logs
int selectedOption = 0; // Options: 0 = logs, 1 = table view, 2 = exit
pid_t process = -1;     // Process id of the digital engine. Used to kill it when the program exits
bool view = true;       // true = logs, false = table
bool fatal = false;     // Whether the program is ending because of a fatal error from the taxi

// First column of the table
const char *headers[NUM_HEADERS] = {
    "Position", "Going towards", "Service",    "Taxi status",
    "Reason",   "Sensor status", "Last order", "Last order status",
};

// Second column of the table
char rows[NUM_HEADERS][150] = {
    "", "", "", "", "", "", "", "",
};

// Whether to print the rows in green (true) or red (false)
bool rowsStatus[NUM_HEADERS] = {true, true, true, true, true, true, true, true};

void ncursesGui(int p) {
  initscr();
  curs_set(0);
  noecho();
  cbreak();
  start_color_wrapper();
  refresh();

  logs = newQueue();
  fd_set readfds;
  char buffer[BUFFER_SIZE];

  int maxx = getmaxx(stdscr);
  int maxy = getmaxy(stdscr);

  menu = newwin(HEIGHT, WIDTH, 0, maxx - WIDTH);
  content = newwin(maxy - 1, maxx - WIDTH - 1, 1, 1);
  wtimeout(menu, 0);
  scrollok(content, TRUE);
  keypad(menu, TRUE);
  printMenu();

  while (true) {
    int c = wgetch(menu);

    if (c != ERR) {
      if (c == ctrl('c') || c == ctrl('z') || c == 'Q' || c == 'q' || !handleInput(c)) {
        break;
      }
    }

    if (view) {
      printLogs();
    } else {
      printTableView();
    }

    FD_ZERO(&readfds);
    FD_SET(p, &readfds);
    if (select(p + 1, &readfds, NULL, NULL, &(struct timeval){0, 100}) > 0) {
      read(p, buffer, BUFFER_SIZE);
      if (buffer[0] == PGUI_END_EXECUTION)
        break;

      if (buffer[0] == PGUI_TAXI_FATAL_ERROR) {
        fatal = true;
        break;
      }

      if (buffer[0] == PGUI_REGISTER_PROCESS) {
        memcpy(&process, buffer + 1, sizeof(pid_t));
        continue;
      }

      if (buffer[0] == PGUI_UPDATE_INFO) {
        parseRows(buffer);
        continue;
      }

      enqueue(logs, buffer);
    }
  }

  if (process != -1)
    kill(process, SIGKILL);
  char *msg = NULL;
  if (fatal) {
    msg = "Fatal error detected.";
  }
  printFinishPopUp(msg);
}

void parseRows(char buffer[BUFFER_SIZE]) {
  int offset = 1;
  Coordinate pos, objective, lastOrderCoord;
  IMPORTANCE importance;
  SUBJECT lastOrder;
  bool canMove, orderedToStop, sensorConnected, lastOrderCompleted;
  int reason;
  char service;

  memcpy(&pos, buffer + offset, sizeof(Coordinate));
  offset += sizeof(Coordinate);
  memcpy(&objective, buffer + offset, sizeof(Coordinate));
  offset += sizeof(Coordinate);

  service = buffer[offset++];
  orderedToStop = buffer[offset++];
  canMove = buffer[offset++];

  memcpy(&importance, buffer + offset, sizeof(IMPORTANCE));
  offset += sizeof(IMPORTANCE);
  memcpy(&reason, buffer + offset, sizeof(int));
  offset += sizeof(int);

  sensorConnected = buffer[offset++];

  memcpy(&lastOrder, buffer + offset, sizeof(SUBJECT));
  offset += sizeof(SUBJECT);
  memcpy(&lastOrderCoord, buffer + offset, sizeof(Coordinate));
  offset += sizeof(Coordinate);

  lastOrderCompleted = buffer[offset++];

  sprintf(rows[0], "[%02i, %02i]", pos.x + 1, pos.y + 1);
  if (orderedToStop) {
    sprintf(rows[1], " - ");
    rowsStatus[1] = false;
  } else {
    sprintf(rows[1], "[%02i, %02i]", objective.x + 1, objective.y + 1);
    rowsStatus[1] = true;
  }
  if (service == -1) {
    sprintf(rows[2], " - ");
    rowsStatus[2] = false;
  } else {
    rows[2][0] = service;
    rows[2][1] = '\0';
    rowsStatus[2] = true;
  }
  sprintf(rows[3], "%s", orderedToStop ? "Stopped" : canMove ? "Moving" : "Can't move");
  rowsStatus[3] = !orderedToStop && canMove;
  if (canMove || !sensorConnected) {
    sprintf(rows[4], " - ");
    rowsStatus[4] = true;
  } else {
    sprintf(rows[4], "%s", inconveniences[importance][reason]);
    rowsStatus[4] = false;
  }
  if (sensorConnected) {
    sprintf(rows[5], "Connected");
    rowsStatus[5] = true;
  } else {
    sprintf(rows[5], "Disconnected");
    rowsStatus[5] = false;
  }
  if (lastOrder == -1) {
    sprintf(rows[6], " - ");
    rowsStatus[6] = false;
  } else {
    sprintf(rows[6], "%s",
            lastOrder == TRESPONSE_GOTO              ? "Go to"
            : lastOrder == TRESPONSE_CHANGE_POSITION ? "Change position to"
            : lastOrder == TRESPONSE_CONTINUE        ? "Continue"
                                                     : "Stop");
    if (lastOrder == TRESPONSE_GOTO || lastOrder == TRESPONSE_CHANGE_POSITION) {
      char coord[30];
      sprintf(coord, " [%02i, %02i]", pos.x + 1, pos.y + 1);
      strcat(rows[6], coord);
      // sprintf(rows[6], "%s [%02i, %02i]", rows[6], lastOrderCoord.x + 1, lastOrderCoord.y + 1);
    }
    rowsStatus[6] = true;
  }
  if (lastOrder == TRESPONSE_GOTO) {
    sprintf(rows[7], "%s", lastOrderCompleted ? "Completed" : "In progress");
    rowsStatus[7] = true;
  } else {
    sprintf(rows[7], " - ");
    rowsStatus[7] = false;
  }
}

void printLogs() {
  QueueIterator it = initQueueIterator(logs);
  char buffer[BUFFER_SIZE];
  werase(content);
  while (getNext(logs, &it, buffer)) {
    if (buffer[1] != 0)
      wattron(content, COLOR_PAIR(buffer[1]));
    waddstr(content, buffer + 2);
    if (buffer[1] != 0)
      wattroff(content, COLOR_PAIR(buffer[1]));
  }
  wrefresh(content);
}

bool handleInput(int c) {
  if (c == KEY_DOWN || c == 's') {
    selectedOption = (selectedOption + 1) % 3;
  } else if (c == KEY_UP || c == 'w') {
    selectedOption = (selectedOption + 2) % 3;
  } else if (c == ' ' || c == '\n') {
    view = selectedOption == 0;
    return selectedOption != 2;
  }
  printMenu();

  return true;
}

void printMenu() {
  werase(menu);
  wborder(menu, ACS_VLINE, ' ', ' ', ACS_HLINE, ACS_VLINE, ' ', ACS_LLCORNER, ACS_HLINE);
  mvwprintw(menu, 1, 3, "[%c] Logs", selectedOption == 0 ? 'X' : ' ');
  mvwprintw(menu, 2, 3, "[%c] Table", selectedOption == 1 ? 'X' : ' ');
  mvwprintw(menu, 3, 3, "[%c] Exit", selectedOption == 2 ? 'X' : ' ');
  wrefresh(menu);
}

void printLine(WINDOW *win, int width, int mid, chtype left, chtype midChar, chtype right,
               chtype fill) {
  waddch(win, left);
  for (int i = 1; i < width - 1; i++) {
    if (i == mid)
      waddch(win, midChar);
    else
      waddch(win, fill);
  }
  waddch(win, right);
}

void printTableView() {
  werase(content);

  const int maxx = getmaxx(stdscr);
  const int maxy = getmaxy(stdscr);
  const int rowLength = 40;
  const int headerLength = 20;
  const int start_x = (maxx - WIDTH - rowLength - headerLength - 3) / 2;
  int height = NUM_HEADERS + 1;
  char lines[MAX_LINES][rowLength - 2];
  for (int i = 0; i < NUM_HEADERS; i++)
    height += split(lines, rows[i], rowLength - 2);
  const int start_y = (maxy - height) / 2 - 1;

  wattron(content, COLOR_PAIR(PASTEL_BLUE));
  wmove(content, start_y, start_x);
  printLine(content, rowLength + headerLength + 3, headerLength + 2, ACS_ULCORNER, ACS_TTEE,
            ACS_URCORNER, ACS_HLINE);

  int offset = 0;

  for (int i = 0; i < NUM_HEADERS; i++) {
    int n = split(lines, rows[i], rowLength - 2);
    offset += n - 1;
    wattron(content, COLOR_PAIR(PASTEL_BLUE));

    for (int j = 0; j < n; j++) {
      wmove(content, start_y + i * 2 + 2 + offset - n + j, start_x);
      printLine(content, rowLength + headerLength + 3, headerLength + 2, ACS_VLINE, ACS_VLINE,
                ACS_VLINE, ' ');
    }
    wmove(content, start_y + (i + 1) * 2 + offset, start_x);
    printLine(content, rowLength + headerLength + 3, headerLength + 2, ACS_LTEE, ACS_PLUS, ACS_RTEE,
              ACS_HLINE);
    mvwaddstr(content, start_y + i * 2 + 2 + offset - n, start_x + 2, headers[i]);

    if (rowsStatus[i])
      wattron(content, COLOR_PAIR(PASTEL_GREEN));
    else
      wattron(content, COLOR_PAIR(PASTEL_RED));

    for (int j = 0; j < n; j++) {
      mvwaddstr(content, start_y + i * 2 + 2 + offset - n + j, start_x + 4 + headerLength,
                lines[j]);
    }
    wattroff(content, COLOR_PAIR(PASTEL_GREEN));
  }

  wattron(content, COLOR_PAIR(PASTEL_BLUE));
  wmove(content, start_y + NUM_HEADERS * 2 + offset, start_x);
  printLine(content, rowLength + headerLength + 3, headerLength + 2, ACS_LLCORNER, ACS_BTEE,
            ACS_LRCORNER, ACS_HLINE);

  wrefresh(content);
}