#include "common.h"

void Agent::deserialize(int src) {
  int mask1 = 0x01;
  int mask2 = 0x03;
  int mask3 = 0x07;
  int mask5 = 0x1F;
  int mask7 = 0x7F;
  int mask8 = 0xFF;

  this->type = (AGENT_TYPE)(src & mask2);
  this->canMove = (src >> 2) & mask1;
  this->status = (AGENT_STATUS)((src >> 3) & mask3);
  this->coord.x = (src >> 6) & mask5;
  this->coord.y = (src >> 11) & mask5;
  this->id = (src >> 16) & mask7;
  this->obj = (src >> 23) & mask8;
  this->carryingCustomer = (src >> 31) & mask1;
}

Address::Address() {
  this->ip = "";
  this->port = 0;
}

void Address::parse(std::string str) {
  size_t n = str.find(':');

  if (n == -1) {
    return;
  }

  for (size_t i = n + 1; i < str.length(); i++) {
    if (str[i] < '0' || str[i] > '9') {
      return;
    }
  }

  this->ip = str.substr(0, n);
  this->port = atoi(str.substr(n + 1, str.length() - n - 1).c_str());
}