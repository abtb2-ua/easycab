#include "raylib.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Core parameters (not prepared for customization)
#define MARGIN 50                             // 50
#define GUTTER 10                             // 10
#define PADDING 7                             // 7
#define FRAME_BORDER_SIZE 5                   // 5
#define MAP_SIZE 900                          // 900
#define GRID_SIZE 20                          // 20
#define CELL_SIZE (int)(MAP_SIZE / GRID_SIZE) // (int)(MAP_SIZE / GRID_SIZE)
#define FRAME_ROUNDNESS .025
#define CELL_ROUNDNESS .1
#define CELL_BORDER_THICKNESS 3
#define LETTER_SPACING 2
#define FONT_SIZE_1 30
#define FONT_SIZE_2 26
#define FONT_SIZE_3 20

// Styling options
#define FULL_BACKGROUND false  // false
#define BACKGROUND_INTENSITY 1 // 1
#define MAX_FPS 60             // 60
#define FPS_X 10               // 10
#define FPS_Y 10               // 10
#define SEGMENTS 10            // 10
#define HLABELS 2              // 2
#define VLABELS 2              // 2
#define LABELS_GAP 2           // 2

// Globals
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

struct ColorPalette {
  Color border, background;
};

struct {
  struct ColorPalette red, green, blue, yellow, blank;
} COLORS;

struct ColorPalette global_colors[GRID_SIZE][GRID_SIZE];
char global_content[GRID_SIZE][GRID_SIZE][4];
static pthread_mutex_t mut_mapContent = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mut_finishFlag = PTHREAD_MUTEX_INITIALIZER;
bool finish = false;

void init();
void DrawLabels();
void *update();

int main() {
  InitWindow(MARGIN * 2 + MAP_SIZE, MARGIN * 2 + MAP_SIZE, "Mapa");
  SetTargetFPS(MAX_FPS);
  init();

  pthread_t thread;
  pthread_create(&thread, NULL, update, NULL);

  Font font = LoadFont("fonts/jupiter_crash.png");

  Rectangle cells[GRID_SIZE][GRID_SIZE] = {0};
  Rectangle background = {0, 0, MARGIN * 2 + MAP_SIZE, GetScreenWidth()};
  Rectangle frame = (Rectangle){MARGIN - PADDING, MARGIN - PADDING,
                                MAP_SIZE + PADDING * 2, MAP_SIZE + PADDING * 2};

  struct ColorPalette colors[GRID_SIZE][GRID_SIZE];
  char content[GRID_SIZE][GRID_SIZE][4];

  if (!FULL_BACKGROUND) {
    background = frame;
  }

  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      cells[i][j].x = MARGIN + (float)GUTTER / 2 + i * CELL_SIZE;
      cells[i][j].y = MARGIN + (float)GUTTER / 2 + j * CELL_SIZE;
      cells[i][j].width = CELL_SIZE - GUTTER;
      cells[i][j].height = CELL_SIZE - GUTTER;
    }
  }

  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(RAYWHITE);

    DrawFPS(FPS_X, FPS_Y);

    DrawRectangleGradientEx(background, Fade(GREEN, .15 * BACKGROUND_INTENSITY),
                            Fade(GREEN, .3 * BACKGROUND_INTENSITY),
                            Fade(SKYBLUE, .45 * BACKGROUND_INTENSITY),
                            Fade(SKYBLUE, .4 * BACKGROUND_INTENSITY));

    DrawRectangleRoundedLines(frame, FRAME_ROUNDNESS, SEGMENTS,
                              FRAME_BORDER_SIZE, DARKGRAY);

    DrawLabels();

    pthread_mutex_lock(&mut_mapContent);
    memcpy(colors, global_colors, sizeof(global_colors));
    memcpy(content, global_content, sizeof(global_content));
    pthread_mutex_unlock(&mut_mapContent);

    for (int i = 0; i < GRID_SIZE; i++) {
      for (int j = 0; j < GRID_SIZE; j++) {
        DrawRectangleRoundedLines(cells[i][j], CELL_ROUNDNESS, SEGMENTS,
                                  CELL_BORDER_THICKNESS, colors[i][j].border);
        DrawRectangleRounded(cells[i][j], CELL_ROUNDNESS, SEGMENTS,
                             colors[i][j].background);

        int fontSize = strlen(content[i][j]) == 1   ? FONT_SIZE_1
                       : strlen(content[i][j]) == 2 ? FONT_SIZE_2
                                                    : FONT_SIZE_3;
        Vector2 v = MeasureTextEx(font, content[i][j], fontSize, 2);
        DrawTextEx(
            font, content[i][j],
            (Vector2){(int)(MARGIN + i * CELL_SIZE + CELL_SIZE / 2. - v.x / 2),
                      (int)(MARGIN + j * CELL_SIZE + CELL_SIZE / 2. - v.y / 2)},
            fontSize, LETTER_SPACING, DARKGRAY);
      }
    }

    EndDrawing();
  }

  pthread_mutex_lock(&mut_finishFlag);
  finish = true;
  pthread_mutex_unlock(&mut_finishFlag);
  pthread_join(thread, NULL);
  pthread_mutex_destroy(&mut_finishFlag);
  pthread_mutex_destroy(&mut_mapContent);

  printf("Exiting...\n");
  CloseWindow();
  return 0;
}
// clang-format on

void init() {
  COLORS.green =
      (struct ColorPalette){.background = Fade(GREEN, .8), .border = DARKGREEN};
  COLORS.blue =
      (struct ColorPalette){.background = Fade(BLUE, .5), .border = DARKBLUE};
  COLORS.yellow =
      (struct ColorPalette){.background = Fade(YELLOW, .8), .border = ORANGE};
  COLORS.red =
      (struct ColorPalette){.background = Fade(RED, .5), .border = MAROON};
  COLORS.blank =
      (struct ColorPalette){.background = BLANK, .border = Fade(DARKGRAY, .9)};

  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      strcpy(global_content[i][j], "");
      global_colors[i][j] = COLORS.blank;
    }
  }
}

void DrawLabels() {
  for (int i = max(LABELS_GAP, 1); i <= 20; i += max(LABELS_GAP, 1)) {
    char text[5];
    sprintf(text, "%d", i);

    if (HLABELS >= 1) {
      DrawText(text,
               MARGIN + i * CELL_SIZE - MeasureText(text, 20) / 2 -
                   CELL_SIZE / 2,
               MARGIN - PADDING - 30, 20, DARKGRAY);
    }

    if (VLABELS >= 1) {
      DrawText(text, MARGIN - PADDING - 35,
               MARGIN + i * CELL_SIZE - MeasureText(text, 20) / 2 -
                   CELL_SIZE / 2,
               20, DARKGRAY);
    }

    if (HLABELS >= 2) {
      DrawText(text,
               MARGIN + i * CELL_SIZE - MeasureText(text, 20) / 2 -
                   CELL_SIZE / 2,
               MARGIN + PADDING + 15 + MAP_SIZE, 20, DARKGRAY);
    }

    if (VLABELS >= 2) {
      DrawText(text, MARGIN + PADDING + 15 + MAP_SIZE,
               MARGIN + i * CELL_SIZE - MeasureText(text, 20) / 2 -
                   CELL_SIZE / 2,
               20, DARKGRAY);
    }
  }
}

void *update() {
  bool local_finish = false;
  int i = 0;

  do {
    pthread_mutex_lock(&mut_mapContent);
    global_colors[i][i] = COLORS.red;
    char p[5];
    sprintf(p, "%d", i);
    strcpy(global_content[i][i], p);
    i++;
    pthread_mutex_unlock(&mut_mapContent);

    if (i == GRID_SIZE) {
      i = 0;
    }

    sleep(1);

    pthread_mutex_lock(&mut_finishFlag);
    local_finish = finish;
    pthread_mutex_unlock(&mut_finishFlag);
  } while (!local_finish);

  return NULL;
}