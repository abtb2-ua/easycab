#ifndef TABLE_GUI_H
#define TABLE_GUI_H

#include "common.h"
#include <ncurses.h>
#include <stdbool.h>

#define MAX_ROWS 100
#define MAX_COLS 6
#define MAX_COL_LEN 20

typedef struct TABLE {
  int start_x, start_y;
  char **headers;
  int cols_num;
  char rows[MAX_ROWS][MAX_COLS][MAX_COL_LEN];
  bool rows_status[MAX_ROWS];
  int rows_num;
  char *title;
  int length;
  int *col_lengths;
  int err_color;
} Table;

typedef struct Queue {
  char elements[300][BUFFER_SIZE];
  int head, tail;
} Queue;

typedef struct QueueIterator {
  int i;
} QueueIterator;

void initTable(Table *table, int start_x, int start_y, char **headers,
               int headers_num, char *title, int err_color);
void printTable(Table *table, WINDOW *win);
void addRow(Table *table, const char **row, bool status);
void emptyTable(Table *table);
void destroyTable(Table *table);

Queue *newQueue();
QueueIterator initQueueIterator(Queue *queue);
bool getNext(Queue *queue, QueueIterator *it, char *element);
bool dequeue(Queue *queue, char *element);
bool enqueue(Queue *queue, char *element);

#endif