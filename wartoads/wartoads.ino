#include "src/game.h"

void setup() {
  Serial.setTxBufferSize(4096);
  Serial.begin(921600);
  delay(200);
  gameBegin();
}

void loop() {
  gameLoop();
}
