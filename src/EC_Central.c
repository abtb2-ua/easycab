#include "common.h"
#include "glib.h"
#include "kafka_module.h"
#include "ncurses_gui.h"
#include "socket_module.h"
#include <ncurses.h>

bool RESET_DB = false;
#define FILE_NAME "res/locations.csv"

Address kafka, db;
int gui_pipe[2];

void getEnvVars();
void checkArguments(int argc, char *argv[], int *listenPort);
void readFile();
void read_error(const char *format, ...);

int main(int argc, char *argv[]) {
  // printf("%li", sizeof(Agent));
  // startKafkaServer();
  // return 0;

  int listenPort;
  char buffer[BUFFER_SIZE];

  pipe(gui_pipe);

  g_log_set_default_handler(log_handler, NULL);
  checkArguments(argc, argv, &listenPort);
  getEnvVars();

  pid_t gui_pid = fork();
  if (gui_pid != 0) {
    close(gui_pipe[1]); // Cerrar extremo de escritura del pipe
    ncursesGui(gui_pipe[0]);
    close(gui_pipe[0]);
    exit(0);
  }
  close(gui_pipe[0]);

  pid_t pid = fork();
  int args[2] = {PGUI_WRITE_TOP_WINDOW, gui_pipe[1]};
  g_log_set_default_handler(ncurses_log_handler, args);

  // signal(SIGINT, exit);
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
    readFile();
  }

  startKafkaServer();
}

void checkArguments(int argc, char *argv[], int *listenPort) {
  char usage[100];

  sprintf(usage, "Usage: %s <listen port> <kafka IP:port> <database IP:port>",
          argv[0]);

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

void readFile() {
  FILE *file = fopen(FILE_NAME, "r");
  if (file == NULL) {
    read_error("Error opening file %s", FILE_NAME);
  }

  mysql_library_init(0, NULL, NULL);
  MYSQL *conn = mysql_init(NULL);
  mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, (int[]){2});

  if (!mysql_real_connect(conn, db.ip, "root", DB_PASSWORD, DB_NAME, db.port,
                          NULL, 0)) {
    read_error("Error connecting to database");
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
    if (sscanf(line, "%c,%d,%d", &id, &x, &y) != 3 || x < 1 || x > 20 ||
        y < 1 || y > 20 || id < 'A' || id > 'Z')
      g_warning("Invalid location. Skipping to the next one");
    x--;
    y--;

    sprintf(query, "INSERT INTO locations (id, x, y) VALUES ('%c', %d, %d)", id,
            x, y);

    if (mysql_query(conn, query)) {
      g_warning("Error inserting location %c (%d, %d): %s", id, x, y,
                mysql_error(conn));
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

  if (reset != NULL && strcmp(reset, "true") == 0)
    RESET_DB = true;
}