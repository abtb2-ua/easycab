
#include "common.h"
#include "glib.h"
#include "kafka_module.h"
#include "ncurses_gui.h"
#include "socket_module.h"
#include <time.h>
#include <unistd.h>

Address kafka, db;
int gui_pipe[2];

void checkArguments(int argc, char *argv[], int *listenPort);

int main(int argc, char *argv[]) {
  // g_log_set_default_handler(log_handler, NULL);
  // listenSocket(8000);
  // exit(0);

  int listenPort;
  char buffer[BUFFER_SIZE];

  pipe(gui_pipe);

  g_log_set_default_handler(log_handler, NULL);
  checkArguments(argc, argv, &listenPort);

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

  if (pid == 0) { // Child

    buffer[0] = PGUI_REGISTER_PROCESS;
    memcpy(buffer + 1, (pid_t[]){getpid()}, sizeof(pid_t));
    write(gui_pipe[1], buffer, BUFFER_SIZE);

    args[0] = PGUI_WRITE_BOTTOM_WINDOW;
    g_log_set_default_handler(ncurses_log_handler, args);

    g_message("GUI process created with PID %i", getpid());

    signal(SIGINT, exit);
    listenSocket(listenPort);

    pause();

    exit(0);
  }

  buffer[0] = PGUI_REGISTER_PROCESS;
  memcpy(buffer + 1, (pid_t[]){getpid()}, sizeof(pid_t));
  write(gui_pipe[1], buffer, BUFFER_SIZE);

  while (true) {
    g_message("GUI process created with PID %i", getpid());
    usleep(1100 * 1000);
  }

  write(gui_pipe[1], (char[]){PGUI_END_EXECUTION}, BUFFER_SIZE);
  close(gui_pipe[1]);
  return 0;
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