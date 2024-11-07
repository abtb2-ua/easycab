#include "common.h"
#include "glib.h"
#include "kafka_module.h"
#include "ncurses_common.h"
#include "ncurses_gui.h"
#include "socket_module.h"
#include <mysql/mysql.h>
#include <ncurses.h>
#include <string.h>

// File name where the locations are stored
char *fileName = "res/locations.csv";

// Whether to start a new session and restart the database
static bool RESET_DB = false;
char session[UUID_LENGTH];

Address kafka, db;
// Pipe to the process that will handle the ncurses gui
int gui_pipe[2];

/// @brief Parses the RESET_DB environment variable
void getEnvVars();

/// @brief Parses the arguments passed to the program
///
/// @param argc Number of arguments
/// @param argv Array of arguments
/// @param listenPort Port the socket module will listen petitions on
void checkArguments(int argc, char *argv[], int *listenPort);

/// @brief Reads and stores into the database the locations written in the file specified by
/// FILE_NAME
///
/// @param conn Pointer to the connection to the database
void readFile(MYSQL *conn);

/// @brief Connects to the database
///
/// @param conn Output argument. Pointer to the connection to initialize
void initConnection(MYSQL **conn);

/// @brief Reads the session from the database or creates a new one if it's a new session. If
/// RESET_DB is true, it will be considered a new session
///
/// @param conn Pointer to the connection to the database
void initSession(MYSQL *conn);

/// @brief Prints an error message and indicates to the ncurses gui interface that the program
/// should end
///
/// @param format Format string
/// @param ... Arguments to be passed to the format string
void read_error(const char *format, ...);

int main(int argc, char *argv[]) {
  MYSQL *conn = NULL;

  int listenPort;
  char buffer[BUFFER_SIZE];

  pipe(gui_pipe);

  g_log_set_default_handler(log_handler, NULL);
  checkArguments(argc, argv, &listenPort);
  getEnvVars();

  initConnection(&conn);
  initSession(conn);

  pid_t gui_pid = fork();
  if (gui_pid != 0) {
    close(gui_pipe[1]);
    ncursesGui(gui_pipe[0]);
    close(gui_pipe[0]);
    exit(0);
  }
  close(gui_pipe[0]);

  pid_t pid = fork();
  int args[2] = {PGUI_WRITE_TOP_WINDOW, gui_pipe[1]};
  g_log_set_default_handler(ncurses_log_handler, args);

  if (pid == 0) { // Child
    args[0] = PGUI_WRITE_BOTTOM_WINDOW;
    g_log_set_default_handler(ncurses_log_handler, args);

    g_debug("process with PID %i", getpid());
    buffer[0] = PGUI_REGISTER_PROCESS;
    memcpy(buffer + 1, (pid_t[]){getpid()}, sizeof(pid_t));
    write(gui_pipe[1], buffer, BUFFER_SIZE);

    listenSocket(listenPort);

    pause();

    exit(0);
  }

  g_debug("process with PID %i", getpid());
  buffer[0] = PGUI_REGISTER_PROCESS;
  memcpy(buffer + 1, (pid_t[]){getpid()}, sizeof(pid_t));
  write(gui_pipe[1], buffer, BUFFER_SIZE);

  if (RESET_DB) {
    readFile(conn);
  }

  startKafkaServer();
}

void checkArguments(int argc, char *argv[], int *listenPort) {
  char usage[100];

  sprintf(usage, "Usage: %s <listen port> <kafka IP:port> <database IP:port>", argv[0]);

  if (argc < 4)
    g_error("%s", usage);

  if (sscanf(argv[1], "%d", listenPort) != 1)
    g_error("Invalid listen port. %s", usage);

  if (sscanf(argv[2], "%[^:]:%d", kafka.ip, &kafka.port) != 2)
    g_error("Invalid kafka address. %s", usage);

  if (sscanf(argv[3], "%[^:]:%d", db.ip, &db.port) != 2)
    g_error("Invalid database address. %s", usage);

  if (*listenPort < 1 || *listenPort > 65535)
    g_error("Invalid listen port. %s", usage);

  if (kafka.port < 1 || kafka.port > 65535)
    g_error("Invalid kafka port. %s", usage);

  if (db.port < 1 || db.port > 65535)
    g_error("Invalid database port. %s", usage);
}

void read_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  g_critical(format, args);
  va_end(args);

  char buffer[BUFFER_SIZE];
  buffer[0] = PGUI_END_EXECUTION;
  write(gui_pipe[1], buffer, BUFFER_SIZE);
  close(gui_pipe[1]);
  exit(1);
}

void initConnection(MYSQL **conn) {
  mysql_library_init(0, NULL, NULL);
  *conn = mysql_init(NULL);
  mysql_options(*conn, MYSQL_OPT_CONNECT_TIMEOUT, (int[]){2});

  if (!mysql_real_connect(*conn, db.ip, "root", DB_PASSWORD, DB_NAME, db.port, NULL, 0)) {
    read_error("Error connecting to database");
  }
}

void readFile(MYSQL *conn) {
  FILE *file = fopen(fileName, "r");
  if (file == NULL) {
    g_error("Error opening file %s", fileName);
  }

  char line[10];
  int x, y;
  char id;
  char query[200];
  int counter = 0;

  if (mysql_query(conn, "CALL ResetDB()")) {
    g_warning("Error reseting database: %s", mysql_error(conn));
  }

  while (fgets(line, 10, file) != NULL) {
    if (sscanf(line, "%c,%d,%d", &id, &x, &y) != 3 || x < 1 || x > 20 || y < 1 || y > 20 ||
        id < 'A' || id > 'Z')
      g_warning("Invalid location. Skipping to the next one");
    x--;
    y--;

    sprintf(query, "INSERT INTO locations (id, x, y) VALUES ('%c', %d, %d)", id, x, y);

    if (mysql_query(conn, query)) {
      g_warning("Error inserting location %c (%d, %d): %s", id, x, y, mysql_error(conn));
      continue;
    }

    g_debug("Stored locations %c (%d, %d)", id, x, y);
    counter++;
  }

  g_message("%i locations read and stored successfully", counter);
  mysql_close(conn);
  mysql_library_end();
  fclose(file);
}

void getEnvVars() {
  char *reset = getenv("RESET_DB");
  char *_fileName = getenv("FILE_NAME");

  if (_fileName != NULL)
    fileName = _fileName;

  if (reset != NULL && strcmp(reset, "true") == 0)
    RESET_DB = true;
}

void initSession(MYSQL *conn) {
  if (RESET_DB) {
    generate_unique_id(session);
    char query[100];
    mysql_query(conn, "DELETE FROM session");
    sprintf(query, "INSERT INTO session (id) VALUES ('%s')", session);
    if (mysql_query(conn, query)) {
      g_error("Error initializing session: %s", mysql_error(conn));
    }
  } else {
    mysql_query(conn, "SELECT id FROM session");
    MYSQL_RES *res = mysql_store_result(conn);
    if (res == NULL) {
      g_error("Error reading session: %s", mysql_error(conn));
    }
    if (mysql_num_rows(res) == 0) {
      g_error("Session not found");
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row[0] == NULL) {
      g_error("Session not found");
    }
    strcpy(session, row[0]);
  }
}
