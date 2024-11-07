#include "data_structures.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>

////////////////////////////////////////////////////////////////////////////////////
//// Table
////////////////////////////////////////////////////////////////////////////////////

void initTable(Table *table, Coordinate start, char *title, char **headers, int headers_num,
               int err_color) {
  table->cols_num = headers_num;
  table->start = start;
  table->headers = headers;
  table->title = title;
  table->length = 1;
  table->rows_num = 0;
  table->col_lengths = malloc(sizeof(int) * headers_num);
  table->err_color = err_color;

  for (int i = 0; i < table->cols_num; i++) {
    table->length += strlen(table->headers[i]) + 3;
    table->col_lengths[i] = table->length - 1;
  }
}

void printTable(Table *table, WINDOW *win) {
  int currentLine = 0;

  mvwaddch(win, table->start.y, table->start.x, ACS_ULCORNER);
  for (int i = 0; i < table->length - 2; i++) {
    waddch(win, ACS_HLINE);
  }
  waddch(win, ACS_URCORNER);
  currentLine++;

  mvwaddch(win, table->start.y + currentLine, table->start.x, ACS_VLINE);
  mvwaddch(win, table->start.y + currentLine, table->start.x + table->length - 1, ACS_VLINE);
  mvwaddstr(win, table->start.y + currentLine,
            table->start.x + (table->length - strlen(table->title)) / 2, table->title);
  currentLine++;

  mvwaddch(win, table->start.y + currentLine, table->start.x, ACS_LTEE);
  for (int i = 0; i < table->length - 2; i++) {
    waddch(win, ACS_HLINE);
  }
  waddch(win, ACS_RTEE);
  for (int i = 0; i < table->cols_num - 1; i++) {
    mvwaddch(win, table->start.y + currentLine, table->start.x + table->col_lengths[i], ACS_TTEE);
  }
  currentLine++;

  mvwaddch(win, table->start.y + currentLine, table->start.x, ACS_VLINE);
  for (int i = 0; i < table->cols_num; i++) {
    wprintw(win, " %s ", table->headers[i]);
    waddch(win, ACS_VLINE);
  }
  currentLine++;

  mvwaddch(win, table->start.y + currentLine, table->start.x, ACS_LTEE);
  for (int i = 0; i < table->length - 2; i++) {
    waddch(win, ACS_HLINE);
  }
  waddch(win, ACS_RTEE);
  for (int i = 0; i < table->cols_num - 1; i++) {
    mvwaddch(win, table->start.y + currentLine, table->start.x + table->col_lengths[i], ACS_PLUS);
  }
  currentLine++;

  for (int i = 0; i < table->rows_num; i++) {
    int last = 0;
    mvwaddch(win, table->start.y + currentLine, table->start.x, ACS_VLINE);
    for (int j = 0; j < table->cols_num; j++) {
      mvwaddch(win, table->start.y + currentLine, table->start.x + table->col_lengths[j],
               ACS_VLINE);
    }
    if (!table->rows_status[i])
      wattron(win, COLOR_PAIR(table->err_color));
    for (int j = 0; j < table->cols_num; j++) {
      int start = (table->col_lengths[j] - last - strlen(table->rows[i][j]));
      start += start % 2;
      mvwaddstr(win, table->start.y + currentLine, table->start.x + last + start / 2,
                table->rows[i][j]);
      last = table->col_lengths[j];
    }
    wattroff(win, COLOR_PAIR(table->err_color));
    currentLine++;
  }

  mvwaddch(win, table->start.y + currentLine, table->start.x, ACS_LLCORNER);
  for (int i = 0; i < table->length - 2; i++) {
    waddch(win, ACS_HLINE);
  }
  waddch(win, ACS_LRCORNER);
  for (int i = 0; i < table->cols_num - 1; i++) {
    mvwaddch(win, table->start.y + currentLine, table->start.x + table->col_lengths[i], ACS_BTEE);
  }
}

void addRow(Table *table, const char **row, bool status) {
  for (int i = 0; i < table->cols_num; i++) {
    strncpy(table->rows[table->rows_num][i], row[i], MAX_COL_LEN - 1);
    table->rows[table->rows_num][i][MAX_COL_LEN - 1] = '\0';
  }
  table->rows_status[table->rows_num] = status;
  table->rows_num++;
}

void emptyTable(Table *table) { table->rows_num = 0; }

void destroyTable(Table *table) { free(table->col_lengths); }

////////////////////////////////////////////////////////////////////////////////////
//// Queue
////////////////////////////////////////////////////////////////////////////////////

Queue *newQueue() {
  Queue *queue = malloc(sizeof(Queue));
  queue->head = 0;
  queue->tail = 0;

  for (int i = 0; i < QUEUE_SIZE; i++) {
    queue->elements[i][0] = '\0';
  }
  return queue;
}

QueueIterator initQueueIterator(Queue *queue) {
  QueueIterator it;
  it.i = queue->head;
  return it;
}

bool getNext(Queue *queue, QueueIterator *it, void *element) {
  if (it->i == queue->tail)
    return false;
  memcpy(element, queue->elements[it->i], BUFFER_SIZE);
  it->i = (it->i + 1) % QUEUE_SIZE;
  return true;
}

bool dequeue(Queue *queue, void *element) {
  if (queue->head == queue->tail)
    return false;
  memcpy(element, queue->elements[queue->head], BUFFER_SIZE);

  queue->head = (queue->head + 1) % QUEUE_SIZE;
  return true;
}

void enqueue(Queue *queue, void *element) {
  char placeholder[BUFFER_SIZE];
  if ((queue->tail + 2) % QUEUE_SIZE == queue->head)
    dequeue(queue, placeholder);

  memcpy(queue->elements[queue->tail], element, BUFFER_SIZE);
  queue->tail = (queue->tail + 1) % QUEUE_SIZE;
}