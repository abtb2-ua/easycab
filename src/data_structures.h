#ifndef TABLE_GUI_H
#define TABLE_GUI_H

#include "common.h"
#include <ncurses.h>
#include <stdbool.h>

//////////////////////////////////////////////////////////////////////////////////////
/// TABLE                                                                          ///
//////////////////////////////////////////////////////////////////////////////////////

// To avoid any memory leaks as far as possible, table rows are not dynamically allocated.
// As a result of that, the maximum capacity is adjusted to what's going to be used.
// If the application is scaled up, probably table should be refactored

// Max quantity of rows in a table
#define MAX_ROWS 100
// Max quantity of columns in a table
#define MAX_COLS 6
// Max length of the content of a column
#define MAX_COL_LEN 20

/// Represents a table intended to be printed in a ncurses window
typedef struct TABLE {
  Coordinate start; // Represents the top left corner of the table
  int cols_num;
  int rows_num;
  int length;

  char *title; // Title of the table, will be printed above the headers as a "combined cell"
  char **headers;
  char rows[MAX_ROWS][MAX_COLS][MAX_COL_LEN]; // Content of the rows
  bool rows_status[MAX_ROWS]; // Whether the row is erroneous or not. If erroneous, the row will
                              // be printed in a different color

  int *col_lengths; // Character-based length of each column
  int err_color;    // Represents an already defined ncurses color that will be used to decorate the
                    // erroneous rows
} Table;

/// @brief Initializes a table
///
/// @param table Table to be initialized
/// @param start Coordinate where the table will start (top left corner)
/// @param title Title of the table
/// @param headers Array of strings that will be used as headers
/// @param headers_num Number of headers
/// @param err_color Color in which the rows will be printed if set as erroneous
void initTable(Table *table, Coordinate start, char *title, char **headers, int headers_num,
               int err_color);

/// @brief Prints a table in a ncurses window
///
/// @param table Table to be printed
/// @param win Window in which the table will be printed
void printTable(Table *table, WINDOW *win);

/// @brief Adds a row to a table
///
/// @param table Table to which the row will be added
/// @param row Array of strings that will be used as row's content
/// @param status Whether the row is erroneous or not (if erroneous, the row will be printed in
/// a different color)
void addRow(Table *table, const char **row, bool status);

/// @brief Deletes all the rows from a table
///
/// @param table Table to be emptied
void emptyTable(Table *table);

/// @brief Disposes the memory allocated for a table
///
/// @param table Table to be destroyed
void destroyTable(Table *table);

//////////////////////////////////////////////////////////////////////////////////////
/// QUEUE                                                                          ///
//////////////////////////////////////////////////////////////////////////////////////

// Max capacity of the queue
#define QUEUE_SIZE (100 * 3)

// Represents a FIFO queue
typedef struct {
  char elements[QUEUE_SIZE][BUFFER_SIZE];
  int head, tail;
} Queue;

// Represents an iterator for a queue
typedef struct {
  int i;
} QueueIterator;

/// @brief Returns a new queue
///
/// @return Queue* Empty queue
Queue *newQueue();

/// @brief Initializes a queue iterator
///
/// @param queue Queue to be iterated
/// @return QueueIterator Iterator to be used
QueueIterator initQueueIterator(Queue *queue);

/// @brief Gets the next element in the queue
///
/// @param queue Queue to be iterated
/// @param it Iterator to be used
/// @param element Pointer to the element to be filled
/// @return true There is an element to be read
/// @return false There aren't anymore elements to be read
bool getNext(Queue *queue, QueueIterator *it, void *element);

/// @brief Pops the first element in the queue (if queue is not empty)
///
/// @param queue Queue to be iterated
/// @param element Pointer to the element to be filled
/// @return true There is an element to be popped
/// @return false The queue is empty
bool dequeue(Queue *queue, void *element);

/// @brief Pushes an element in the queue. If the queue is full, the oldest element will be
/// popped
///
/// @param queue Queue to be iterated
/// @param element Pointer to the element to be pushed
void enqueue(Queue *queue, void *element);

#endif