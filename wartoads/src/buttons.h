#pragma once
#include "config.h"

struct Buttons {
  bool down[BTN_COUNT];
  bool pressed[BTN_COUNT];
  bool released[BTN_COUNT];
  bool repeat[BTN_COUNT];
  uint16_t heldMs[BTN_COUNT];
};

void buttonsBegin();
void buttonsPoll();
const Buttons& buttons();
bool btnDown(uint8_t i);
bool btnPressed(uint8_t i);
bool btnReleased(uint8_t i);
bool btnRepeat(uint8_t i);
uint16_t btnHeld(uint8_t i);

// Physical pin index 0..5 matching PCB silk (L R UP DN OK BACK).
int buttonsRawPressed();
bool buttonsAnyRawDown();
void buttonsApplyMap(const uint8_t physOfLogical[BTN_COUNT]);
void buttonsResetMap();
