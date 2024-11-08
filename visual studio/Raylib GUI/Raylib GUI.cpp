#include "common.h"
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <time.h>

#include <librdkafka/rdkafkacpp.h>
#include <raylib.h>

using namespace std;

// Core parameters (not prepared for customization)
#define MARGIN 50                                 // 50
#define GUTTER 10                                 // 10
#define PADDING 7                                 // 7
#define FRAME_BORDER_SIZE 5                       // 5
#define GUI_MAP_SIZE 900                          // 900
#define GRID_SIZE 20                              // 20
#define CELL_SIZE (int)(GUI_MAP_SIZE / GRID_SIZE) // (int)(GUI_MAP_SIZE / GRID_SIZE)
#define FRAME_ROUNDNESS .025f
#define CELL_ROUNDNESS .1f
#define CELL_BORDER_THICKNESS 3
#define LETTER_SPACING 2
#define FONT_SIZE_1 30.f
#define FONT_SIZE_2 26.f
#define FONT_SIZE_3 20.f
#define FONT_SIZE_4 19.f

// Styling options
#define FULL_BACKGROUND false     // false
#define BACKGROUND_INTENSITY 1.25 // 1
#define MAX_FPS 60                // 60
#define FPS_X 10                  // 10
#define FPS_Y 10                  // 10
#define SEGMENTS 10               // 10
#define HLABELS 2                 // 2
#define VLABELS 2                 // 2
#define LABELS_GAP 2              // 2

// Globals
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

typedef struct ColorPalette {
  Color border, background;
} ColorPalette;

struct {
  ColorPalette red, green, blue, yellow, blank;
} PALLETES;

ColorPalette global_colors[GRID_SIZE][GRID_SIZE];
std::string global_content[GRID_SIZE][GRID_SIZE];
static std::mutex mut_mapContent;
static std::mutex mut_finishFlag;
Address kafka;
bool finish = false;

void Init();
int Update(void *args);
ColorPalette GetPalette(Agent *agent);
void GetContent(Agent *agent, std::string &content);
void DrawLabels();

int foo(void *args) { return 0; }

int main(int argc, const char *argv[]) {
  if (argc != 2) {
    cerr << "Usage: ./build_name <Kafka IP:Port>";
    return 0;
  }

  kafka.parse(argv[1]);

  if (kafka.ip == "" || kafka.port == 0)
    cerr << "Invalid kafka address.";
  cout << kafka.ip << ":" << kafka.port;

  InitWindow(MARGIN * 2 + GUI_MAP_SIZE, MARGIN * 2 + GUI_MAP_SIZE, "Mapa");
  SetTargetFPS(MAX_FPS);

  Init();

  void *args = NULL;

  Font font = LoadFont("jupiter_crash.png");

  thread thr(Update, ref(args));

  Rectangle cells[GRID_SIZE][GRID_SIZE]{};
  Rectangle background = {0, 0, MARGIN * 2 + GUI_MAP_SIZE, (float)GetScreenWidth()};
  Rectangle frame = {MARGIN - PADDING, MARGIN - PADDING, GUI_MAP_SIZE + PADDING * 2,
                     GUI_MAP_SIZE + PADDING * 2};

  struct ColorPalette colors[GRID_SIZE][GRID_SIZE];
  std::string content[GRID_SIZE][GRID_SIZE];

  if (!FULL_BACKGROUND) {
    background = frame;
  }

  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      cells[i][j].y = MARGIN + (float)GUTTER / 2 + i * CELL_SIZE;
      cells[i][j].x = MARGIN + (float)GUTTER / 2 + j * CELL_SIZE;
      cells[i][j].width = CELL_SIZE - GUTTER;
      cells[i][j].height = CELL_SIZE - GUTTER;
    }
  }

  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(RAYWHITE);

    DrawFPS(FPS_X, FPS_Y);

    DrawRectangleGradientEx(
        background, Fade(GREEN, .15 * BACKGROUND_INTENSITY), Fade(GREEN, .3 * BACKGROUND_INTENSITY),
        Fade(SKYBLUE, .45 * BACKGROUND_INTENSITY), Fade(SKYBLUE, .4 * BACKGROUND_INTENSITY));

    DrawRectangleRoundedLines(frame, FRAME_ROUNDNESS, SEGMENTS, FRAME_BORDER_SIZE, DARKGRAY);

    DrawLabels();

    mut_mapContent.lock();
    memcpy(colors, global_colors, sizeof(global_colors));
    memcpy(content, global_content, sizeof(global_content));
    mut_mapContent.unlock();

    for (int i = 0; i < GRID_SIZE; i++) {
      for (int j = 0; j < GRID_SIZE; j++) {
        DrawRectangleRoundedLines(cells[i][j], CELL_ROUNDNESS, SEGMENTS, CELL_BORDER_THICKNESS,
                                  colors[i][j].border);
        DrawRectangleRounded(cells[i][j], CELL_ROUNDNESS, SEGMENTS, colors[i][j].background);

        float fontSize = content[i][j].length() == 1   ? FONT_SIZE_1
                         : content[i][j].length() == 2 ? FONT_SIZE_2
                         : content[i][j].length() == 3 ? FONT_SIZE_3
                                                       : FONT_SIZE_4;

        Vector2 v = MeasureTextEx(font, content[i][j].c_str(), fontSize, 2);
        Vector2 pos;
        pos.x = (float)(MARGIN + j * CELL_SIZE + CELL_SIZE / 2. - v.x / 2);
        pos.y = (float)(MARGIN + i * CELL_SIZE + CELL_SIZE / 2. - v.y / 2);
        DrawTextEx(font, content[i][j].c_str(), pos, fontSize, LETTER_SPACING, DARKGRAY);
      }
    }

    EndDrawing();
  }
  mut_finishFlag.lock();
  finish = true;
  mut_finishFlag.unlock();
  thr.join();

  cout << "Exiting..." << endl;
  CloseWindow();
  return 0;
}

void Init() {
  PALLETES.green.background = Fade(GREEN, .8f);
  PALLETES.green.border = DARKGREEN;

  PALLETES.blue.background = Fade(BLUE, .8f);
  PALLETES.blue.border = DARKBLUE;

  PALLETES.yellow.background = Fade(YELLOW, .8f);
  PALLETES.yellow.border = ORANGE;

  PALLETES.red.background = Fade(RED, .8f);
  PALLETES.red.border = MAROON;

  PALLETES.blank.background = BLANK;
  PALLETES.blank.border = Fade(DARKGRAY, .9f);

  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      global_content[i][j] = "";
      global_colors[i][j] = PALLETES.blank;
    }
  }
}

void DrawLabels() {
  for (int i = max(LABELS_GAP, 1); i <= 20; i += max(LABELS_GAP, 1)) {
    string str = to_string(i);
    const char *text = str.c_str();

    if (HLABELS >= 1) {
      DrawText(text, MARGIN + i * CELL_SIZE - MeasureText(text, 20) / 2 - CELL_SIZE / 2,
               MARGIN - PADDING - 30, 20, DARKGRAY);
    }

    if (VLABELS >= 1) {
      DrawText(text, MARGIN - PADDING - 35,
               MARGIN + i * CELL_SIZE - MeasureText(text, 20) / 2 - CELL_SIZE / 2, 20, DARKGRAY);
    }

    if (HLABELS >= 2) {
      DrawText(text, MARGIN + i * CELL_SIZE - MeasureText(text, 20) / 2 - CELL_SIZE / 2,
               MARGIN + PADDING + 15 + GUI_MAP_SIZE, 20, DARKGRAY);
    }

    if (VLABELS >= 2) {
      DrawText(text, MARGIN + PADDING + 15 + GUI_MAP_SIZE,
               MARGIN + i * CELL_SIZE - MeasureText(text, 20) / 2 - CELL_SIZE / 2, 20, DARKGRAY);
    }
  }
}

int Update(void *param) {
  RdKafka::KafkaConsumer *consumer = NULL;
  std::string address;
  std::string errstr;
  std::string id = "";
  id += "gui-consumer-";
  id += std::to_string(time(NULL));
  address = kafka.ip;
  address += ":";
  address += to_string(kafka.port);
  RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);

#define SET_CONFIG(key, value)                                                                     \
  if (conf->set(key, value, errstr) != RdKafka::Conf::ConfResult::CONF_OK) {                       \
    cerr << "Error configuring kafka consumer: " << errstr;                                        \
    return -1;                                                                                     \
  }

  conf->set("socket.timeout.ms", "60000", errstr); // 60 segundos de timeout
  conf->set("fetch.max.wait.ms", "500", errstr);   // 500ms para esperar mensajes
  cout << address << endl;
  SET_CONFIG("log_level", "0");
  SET_CONFIG("bootstrap.servers", address);
  SET_CONFIG("client.id", id);
  SET_CONFIG("group.id", id);
  SET_CONFIG("acks", "all");
  SET_CONFIG("auto.offset.reset", "earliest");
  SET_CONFIG("session.timeout.ms", "6000");
  SET_CONFIG("max.poll.interval.ms", "6000");

  consumer = RdKafka::KafkaConsumer::create(conf, errstr);
  if (!consumer) {
    cerr << "Failed to create consumer: " << errstr;
    return -1;
  }

  Response response;
  ColorPalette colors[GRID_SIZE][GRID_SIZE]{};
  string content[GRID_SIZE][GRID_SIZE];
  bool local_finish = false;
  vector<string> topics = {"taxi_responses", "customer_responses", "map_responses"};
  RdKafka::ErrorCode err = consumer->subscribe(topics);
  if (err) {
    cerr << "Failed to subscribe to topics: " << RdKafka::err2str(err) << endl;
    return -1;
  }

  while (true) {
    std::unique_ptr<RdKafka::Message> message(consumer->consume(1000));

    if (message->err() == RdKafka::ERR__PARTITION_EOF) {
      std::cout << "Nothing read" << std::endl;
      continue;
    }

    if (message->err() != RdKafka::ERR_NO_ERROR) {
      std::cerr << "Consumer error: " << message->errstr() << message->err() << std::endl;
      continue;
    }

    // std::cout << "Message received: " << static_cast<const char*>(message->payload()) <<
    // std::endl;
    memcpy(&response, static_cast<const char *>(message->payload()), sizeof(response));

    for (int i = 0; i < GRID_SIZE; i++) {
      for (int j = 0; j < GRID_SIZE; j++) {
        colors[i][j] = PALLETES.blank;
        content[i][j][0] = '\0';
      }
    }

    for (int i = 0; response.map[i] != 0; i++) {
      Agent agent{};
      agent.deserialize(response.map[i]);
      if ((agent.type == AGENT_TYPE::CLIENT && agent.status == AGENT_STATUS::CLIENT_IN_TAXI) ||
          (agent.type == AGENT_TYPE::TAXI && agent.status == AGENT_STATUS::TAXI_DISCONNECTED))
        continue;
      colors[agent.coord.x][agent.coord.y] = GetPalette(&agent);
      GetContent(&agent, content[agent.coord.x][agent.coord.y]);
    }

    mut_mapContent.lock();
    memcpy(global_colors, colors, sizeof(colors));
    memcpy(global_content, content, sizeof(content));
    mut_mapContent.unlock();

    mut_finishFlag.lock();
    local_finish = finish;
    mut_finishFlag.unlock();
  }

  consumer->close();
  RdKafka::wait_destroyed(5000); // Clean up resources

  return 0;
}

ColorPalette GetPalette(Agent *agent) {
  switch (agent->type) {
  case AGENT_TYPE::TAXI:
    if (agent->status == AGENT_STATUS::TAXI_STOPPED ||
        agent->status == AGENT_STATUS::TAXI_DISCONNECTED)
      return PALLETES.red;
    return PALLETES.green;
  case AGENT_TYPE::CLIENT:
    return PALLETES.yellow;
  case AGENT_TYPE::LOCATION:
    return PALLETES.blue;
  default:
    return PALLETES.blank;
  }
}

void GetContent(Agent *agent, std::string &content) {
  if (agent->type == AGENT_TYPE::TAXI) {
    // printf("Agent type: %i\n", agent->type);
    // printf("Agent status: %i\n", agent->status);
    // printf("Carrying client: %i\n", agent->carryingCustomer);
    // printf("Agent can move: %i\n", agent->canMove);
    // printf("Id: %i\n", agent->id);
    // printf("\n");
  }

  content = "";

  if (agent->type == AGENT_TYPE::TAXI && !agent->canMove) {
    content += "!";
  }

  content += agent->id;

  if (agent->type == AGENT_TYPE::TAXI && agent->carryingCustomer) {
    content += agent->obj;
  }
}