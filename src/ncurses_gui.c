#include "ncurses_gui.h"
#include "common.h"
#include "data_structures.h"
#include "ncurses_common.h"
#include <bits/pthreadtypes.h>
#include <librdkafka/rdkafka.h>
#include <ncurses.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define STATUS_MARGIN 5
#define MARGIN_BETWEEN_TABLES 2

WINDOW *top_box, *menu_box;
WINDOW *menu_win, *top_win, *bottom_win, *table_win;
extern Address kafka;
int map[MAP_SIZE] = {0};
pthread_mutex_t mut;
pid_t processes[5];
int processCount = 0;
bool changedView = false;
Queue *q_top, *q_bottom;
char *statusTranslations[8];

// Menu options
int selectedView = 0;
int selectedAction = 0;
int selectedOption = 0;
bool showOptions = false;
int selectedTaxi[2] = {-1, -1};
int selectedCoord[4] = {-1, -1, -1, -1};
int showErrorMsg = 0;
rd_kafka_t *producer;
static Request request;

void finish() {
  for (int i = 0; i < processCount; i++)
    kill(processes[i], SIGKILL);

  printFinishPopUp(NULL);

  printf("Processes stopped: %i\n", processCount);
}

void ncursesInit() {
  initscr();
  curs_set(0);
  noecho();
  raw();

  pthread_mutex_init(&mut, NULL);

  q_top = newQueue();
  q_bottom = newQueue();

  start_color_wrapper();

  int maxx, maxy;

  getmaxyx(stdscr, maxy, maxx);

  menu_box = newwin(maxy, WIDTH, 0, 0);
  menu_win = newwin(maxy - 2, WIDTH - 2, 1, 1);
  top_box = newwin(maxy - HEIGHT, maxx - WIDTH + 1, 0, WIDTH - 1);
  top_win = newwin(maxy - HEIGHT - 2, maxx - WIDTH - 1, 1, WIDTH + 1);
  bottom_win = newwin(HEIGHT, maxx - WIDTH - 1, maxy - HEIGHT, WIDTH + 1);
  table_win = newwin(maxy, maxx - WIDTH, 0, WIDTH);
  box(menu_box, 0, 0);

  scrollok(top_win, TRUE);
  scrollok(bottom_win, TRUE);

  wmove(bottom_win, 0, 0);
  wmove(top_win, 0, 0);

  wrefresh(menu_box);

  keypad(menu_win, TRUE);
  wtimeout(menu_win, 0);

  wborder(top_box, ACS_VLINE, ' ', ' ', ACS_HLINE, ACS_BBSS, ' ', ACS_LTEE, ACS_HLINE);
  wrefresh(top_box);

  producer = createKafkaUser(&kafka, RD_KAFKA_PRODUCER, "central-ncurses-gui-producer");

  statusTranslations[STATUS_TAXI_CANT_MOVE] = "Can't move";
  statusTranslations[STATUS_TAXI_STOPPED] = "Stopped";
  statusTranslations[STATUS_TAXI_DISCONNECTED] = "Disconnected";
  statusTranslations[STATUS_CUSTOMER_WAITING_TAXI] = "Waiting taxi";
  statusTranslations[STATUS_CUSTOMER_IN_TAXI] = "In taxi";
  statusTranslations[STATUS_CUSTOMER_IN_QUEUE] = "In queue";
  statusTranslations[STATUS_CUSTOMER_OTHER] = "Doing errands";
}

void ncursesGui(int p) {
  ncursesInit();
  printMenu();

  pthread_t thread;
  pthread_create(&thread, NULL, readMap, NULL);

  while (true) {
    int c = wgetch(menu_win);
    if (c == ERR) {
      wrefresh(top_win);
    } else if (c == 'q' || c == ctrl('c') || c == ctrl('z') || c == 'Q') {
      break;
    }

    handleInput(c);

    if (!printPetition(p))
      break;

    // if (selectedView != lastSelectedView) {
    //   if (selectedView == 0) {
    //     wclear(table_win);
    //   } else {
    //     wclear(top_box);
    //     wclear(top_win);
    //     wclear(bottom_win);
    //   }
    // }

    if (selectedView == 0) {
      printLogs();
    } else {
      printTableView();
    }
  }

  finish();
}

void *readMap() {
  Response response;
  response.map[0] = 0;
  rd_kafka_message_t *msg = NULL;
  rd_kafka_t *consumer = createKafkaUser(&kafka, RD_KAFKA_CONSUMER, "central-ncurses-gui-consumer");
  subscribeToTopics(&consumer,
                    (const char *[]){"taxi_responses", "customer_responses", "map_responses"}, 3);

  while (true) {
    if (msg != NULL)
      rd_kafka_message_destroy(msg);
    msg = rd_kafka_consumer_poll(consumer, 1000);

    if (!msg) {
      char *debug_env = getenv("G_MESSAGES_DEBUG");
      if (debug_env != NULL) {
        char buffer[BUFFER_SIZE];
        buffer[1] = PASTEL_MAGENTA;
        strcpy(buffer + 2, "Debug: Nothing read\n");
        enqueue(q_top, buffer);
      }
      continue;
    }

    if (msg->err) {
      char buffer[BUFFER_SIZE];
      buffer[1] = PASTEL_ORANGE;
      sprintf(buffer + 2, "WARNING: Error: %s\n", rd_kafka_message_errstr(msg));
      enqueue(q_top, buffer);
      continue;
    }

    memcpy(&response, msg->payload, sizeof(response));
    pthread_mutex_lock(&mut);
    memcpy(map, response.map, sizeof(map));
    pthread_mutex_unlock(&mut);
  }

  return NULL;
}

void handleInput(int c) {
  // From the me who wrote this code:
  // Sorry and good luck
  if (c == '\t') {
    selectedView = (selectedView + 1) % 2;
    changedView = true;
    werase(table_win);

    if (selectedView == 0) {
      wborder(top_box, ACS_VLINE, ' ', ' ', ACS_HLINE, ACS_BBSS, ' ', ACS_LTEE, ACS_HLINE);
    } else {

      wborder(top_box, ACS_VLINE, ' ', ' ', ' ', ACS_BBSS, ' ', ACS_VLINE, ' ');
    }
    wrefresh(top_box);
  } else if (showOptions) {
    if (c == 'b') {
      showOptions = false;
      showErrorMsg = 0;
      for (int i = 0; i < 2; i++)
        selectedTaxi[i] = -1;
      for (int i = 0; i < 4; i++)
        selectedCoord[i] = -1;
    } else if (c == KEY_DOWN || c == 's') {
      selectedOption = (selectedOption + 1) % (selectedAction == 0 ? 3 : 2);
    } else if (c == KEY_UP || c == 'w') {
      selectedOption =
          (selectedOption + (selectedAction == 0 ? 3 : 2) - 1) % (selectedAction == 0 ? 3 : 2);
    } else if ((c == ' ' || c == '\n') && ((selectedAction == 0 && selectedOption == 2) ||
                                           (selectedAction != 0 && selectedOption == 1))) {
      showErrorMsg = 0;
      for (int i = 0; i < 2; i++)
        if (selectedTaxi[i] == -1)
          showErrorMsg = 1;
      for (int i = 0; i < 4; i++)
        if (selectedCoord[i] == -1 && selectedAction == 0)
          showErrorMsg = 1;
      if (selectedCoord[0] * 10 + selectedCoord[1] > 20 ||
          selectedCoord[2] * 10 + selectedCoord[3] > 20)
        showErrorMsg = 2;

      if (showErrorMsg == 0) {
        request.subject = selectedAction == 0   ? ORDER_GOTO
                          : selectedAction == 1 ? ORDER_GOTO
                          : selectedAction == 2 ? ORDER_STOP
                                                : ORDER_CONTINUE;

        if (selectedAction == 0) {
          request.coord.x = selectedCoord[0] * 10 + selectedCoord[1] - 1;
          request.coord.y = selectedCoord[2] * 10 + selectedCoord[3] - 1;
        } else if (selectedAction == 1) {
          request.coord.x = 0;
          request.coord.y = 0;
        }
        request.id = selectedTaxi[0] * 10 + selectedTaxi[1];

        sendEvent(producer, "requests", &request, sizeof(request));
        showOptions = false;
        for (int i = 0; i < 2; i++)
          selectedTaxi[i] = -1;
        for (int i = 0; i < 4; i++)
          selectedCoord[i] = -1;
      }
    } else if (c >= '0' && c <= '9') {
      if (selectedOption == 0) {
        for (int i = 0; i < 2; i++) {
          if (selectedTaxi[i] == -1) {
            selectedTaxi[i] = c - '0';
            break;
          }
        }
      } else if (selectedOption == 1 && selectedAction == 0) {
        for (int i = 0; i < 4; i++) {
          if (selectedCoord[i] == -1) {
            selectedCoord[i] = c - '0';
            break;
          }
        }
      }
    } else if (c == KEY_BACKSPACE) {
      if (selectedOption == 0) {
        for (int i = 1; i >= 0; i--) {
          if (selectedTaxi[i] != -1) {
            selectedTaxi[i] = -1;
            break;
          }
        }
      } else if (selectedOption == 1 && selectedAction == 0) {
        for (int i = 3; i >= 0; i--) {
          if (selectedCoord[i] != -1) {
            selectedCoord[i] = -1;
            break;
          }
        }
      }
    }
  } else {
    if (c == KEY_DOWN || c == 's') {
      selectedAction = (selectedAction + 1) % 4;
    } else if (c == KEY_UP || c == 'w') {
      selectedAction = (selectedAction + 3) % 4;
    } else if (c == ' ' || c == '\n') {
      selectedOption = 0;
      showOptions = true;
    }
  }

  printMenu();
}

void printLogs() {
  wmove(top_win, 0, 0);
  wmove(bottom_win, 0, 0);
  char element[BUFFER_SIZE];
  QueueIterator it = initQueueIterator(q_top);

  while (getNext(q_top, &it, element)) {
    int color = element[1];
    if (color != 0)
      wattron(top_win, COLOR_PAIR(color));
    wprintw(top_win, "%s", element + 2);
    if (color != 0)
      wattroff(top_win, COLOR_PAIR(color));
  }

  it = initQueueIterator(q_bottom);
  while (getNext(q_bottom, &it, element)) {
    int color = element[1];
    if (color != 0)
      wattron(bottom_win, COLOR_PAIR(color));
    wprintw(bottom_win, "%s", element + 2);
    if (color != 0)
      wattroff(bottom_win, COLOR_PAIR(color));
  }

  wrefresh(bottom_win);
  wrefresh(top_win);
}

void printTableView() {
  int localMap[MAP_SIZE];
  Table locs, customers, taxis;
  char status[100];
  strcpy(status + STATUS_MARGIN, "Status");
  for (int i = 0; i < STATUS_MARGIN; i++) {
    status[i] = ' ';
    status[i + STATUS_MARGIN + 6] = ' ';
  }
  status[STATUS_MARGIN * 2 + 6] = '\0';

  Coordinate startCoord = {.x = 0, .y = 1};
  initTable(&locs, startCoord, "Locations", (char *[]){"ID", "Coordinate"}, 2, PASTEL_RED);
  initTable(&customers, startCoord, "Customers",
            (char *[]){"ID", "Coordinate", "Destination", status}, 4, PASTEL_RED);
  initTable(&taxis, startCoord, "Taxis", (char *[]){"ID", "Coordinate", "Service", status}, 4,
            PASTEL_RED);

  int totalLength = locs.length + customers.length + taxis.length + MARGIN_BETWEEN_TABLES * 2;
  int maxx = getmaxx(stdscr);
  int start_x = (maxx - WIDTH - totalLength) / 2;
  locs.start.x = start_x;
  customers.start.x = start_x + locs.length + MARGIN_BETWEEN_TABLES;
  taxis.start.x = start_x + locs.length + customers.length + MARGIN_BETWEEN_TABLES * 2;

  pthread_mutex_lock(&mut);
  memcpy(localMap, map, sizeof(localMap));
  pthread_mutex_unlock(&mut);

  char id[4];
  char coord[30];
  char obj[2];
  for (int i = 0; map[i] != 0; i++) {
    Entity entity;
    deserializeEntity(&entity, map[i]);
    sprintf(coord, "[%02i, %02i]", entity.coord.x + 1, entity.coord.y + 1);
    sprintf(id, "%c", entity.id);

    if (entity.type == ENTITY_LOCATION) {
      addRow(&locs, (const char *[]){id, coord}, true);
    } else if (entity.type == ENTITY_CUSTOMER) {
      obj[0] = entity.obj == -1 ? '-' : entity.obj;
      obj[1] = '\0';
      addRow(&customers, (const char *[]){id, coord, obj, statusTranslations[entity.status]}, true);
    } else {
      sprintf(id, "%02i", entity.id);
      obj[0] = entity.obj == -1 ? '-' : entity.obj;
      obj[1] = '\0';
      if (entity.status == STATUS_TAXI_MOVING) {
        addRow(&taxis,
               (const char *[]){id, coord, obj,
                                entity.carryingCustomer ? "Carrying customer" : "Moving"},
               true);
      } else {
        addRow(&taxis, (const char *[]){id, coord, obj, statusTranslations[entity.status]},
               entity.status != STATUS_TAXI_DISCONNECTED);
      }
    }
  }

  werase(table_win);
  printTable(&locs, table_win);
  printTable(&customers, table_win);
  printTable(&taxis, table_win);
  destroyTable(&locs);
  destroyTable(&customers);
  destroyTable(&taxis);
  wrefresh(table_win);
}

void printMenu() {
  // From the me who wrote this code:
  // Sorry and good luck
  werase(menu_win);
  wattron(menu_win, A_BOLD);
  mvwaddstr(menu_win, 1, 1, "VIEWS");
  mvwaddstr(menu_win, 6, 1, "ACTIONS");
  if (showOptions)
    mvwaddstr(menu_win, 12, 1, "OPTIONS");
  wattroff(menu_win, A_BOLD);

  mvwprintw(menu_win, 2, 1, "[%c] Logs", selectedView == 0 ? 'X' : ' ');
  mvwprintw(menu_win, 3, 1, "[%c] Table", selectedView == 1 ? 'X' : ' ');
  mvwprintw(menu_win, 7, 1, " %c%c Go to", selectedAction == 0 ? '>' : ' ',
            (selectedAction == 0 && showOptions) ? '>' : ' ');
  mvwprintw(menu_win, 8, 1, " %c%c Return to base", selectedAction == 1 ? '>' : ' ',
            (selectedAction == 1 && showOptions) ? '>' : ' ');
  mvwprintw(menu_win, 9, 1, " %c%c Stop", selectedAction == 2 ? '>' : ' ',
            (selectedAction == 2 && showOptions) ? '>' : ' ');
  mvwprintw(menu_win, 10, 1, " %c%c Continue", selectedAction == 3 ? '>' : ' ',
            (selectedAction == 3 && showOptions) ? '>' : ' ');
  if (showOptions) {
    mvwprintw(menu_win, 13, 1, " %c  Taxi: %c%c", selectedOption == 0 ? '>' : ' ',
              selectedTaxi[0] == -1 ? '_' : selectedTaxi[0] + '0',
              selectedTaxi[1] == -1 ? '_' : selectedTaxi[1] + '0');
    if (selectedAction == 0) {
      mvwprintw(menu_win, 14, 1, " %c  Coordinate: [%c%c, %c%c]", selectedOption == 1 ? '>' : ' ',
                selectedCoord[0] == -1 ? '_' : selectedCoord[0] + '0',
                selectedCoord[1] == -1 ? '_' : selectedCoord[1] + '0',
                selectedCoord[2] == -1 ? '_' : selectedCoord[2] + '0',
                selectedCoord[3] == -1 ? '_' : selectedCoord[3] + '0');
      mvwprintw(menu_win, 15, 1, " %c  Execute ", selectedOption == 2 ? '>' : ' ');
      if (showErrorMsg) {
        wattron(menu_win, COLOR_PAIR(PASTEL_RED));
        mvwprintw(menu_win, 18, 1,
                  showErrorMsg == 1 ? "Fill the required fields" : "Invalid coordinate");
        wattroff(menu_win, COLOR_PAIR(PASTEL_RED));
      }
    } else {
      mvwprintw(menu_win, 14, 1, " %c  Execute ", selectedOption == 1 ? '>' : ' ');
      if (showErrorMsg) {
        wattron(menu_win, COLOR_PAIR(PASTEL_RED));
        mvwprintw(menu_win, 17, 1, "Fill the required fields");
        wattroff(menu_win, COLOR_PAIR(PASTEL_RED));
      }
    }
  }

  wattron(menu_win, A_DIM);
  if (!changedView)
    mvwaddstr(menu_win, 4, 1, "(Press tab to change view)");

  mvwaddstr(menu_win, getmaxy(menu_win) - 1, 1, "(Press 'q' to exit)");
  if (showOptions)
    mvwaddstr(menu_win, selectedAction == 0 ? 16 : 15, 1, "(Press 'b' to cancel)");
  wattroff(menu_win, A_DIM);

  wrefresh(menu_win);
}

bool printPetition(int p) {
  Queue **q = NULL;
  fd_set readfds;
  char buffer[BUFFER_SIZE];

  FD_ZERO(&readfds);
  FD_SET(p, &readfds);
  if (select(p + 1, &readfds, NULL, NULL, &(struct timeval){0, 100}) > 0) {
    read(p, buffer, BUFFER_SIZE);
    switch (buffer[0]) {
    case PGUI_WRITE_TOP_WINDOW:
      q = &q_top;
      break;

    case PGUI_WRITE_BOTTOM_WINDOW:
      q = &q_bottom;
      break;

    case PGUI_END_EXECUTION:
      return false;

    case PGUI_REGISTER_PROCESS:
      memcpy(&processes[processCount], buffer + 1, sizeof(pid_t));
      processCount++;
      return true;

    default:
      if (buffer[0] == PGUI_WRITE_BOTTOM_WINDOW || buffer[0] == PGUI_WRITE_TOP_WINDOW)
        break;
      return true;
    }

    // if (color != 0) {
    //   wattron(*win, COLOR_PAIR(color));
    //   waddstr(*win, buffer + 2);
    //   wattroff(*win, COLOR_PAIR(color));
    // } else {
    //   waddstr(*win, buffer + 2);
    // }
    if (q != NULL)
      enqueue(*q, buffer);

    // waddstr(bottom_win, buffer + 2);
    // wrefresh(*win);
  }
  return true;
}