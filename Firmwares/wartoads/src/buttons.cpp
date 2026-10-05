#include "buttons.h"
#include <Arduino.h>
#include <Preferences.h>
#include <driver/gpio.h>

static const uint8_t PINS[BTN_COUNT] = {
  PIN_BTN_LEFT, PIN_BTN_RIGHT, PIN_BTN_AIM_UP,
  PIN_BTN_AIM_DN, PIN_BTN_FIRE, PIN_BTN_WEAPON
};

static Buttons st;
static bool stable[BTN_COUNT];
static bool last[BTN_COUNT];
static bool rawPressed[BTN_COUNT];
static uint32_t debounceAt[BTN_COUNT];
static uint32_t lastRepeat[BTN_COUNT];
static uint32_t lastPoll;
static uint8_t physOf[BTN_COUNT];
static Preferences prefs;

static bool validMap(const uint8_t* m) {
  bool used[BTN_COUNT] = {};
  for (int i = 0; i < BTN_COUNT; i++) {
    if (m[i] >= BTN_COUNT || used[m[i]]) return false;
    used[m[i]] = true;
  }
  return true;
}

static void identityMap() {
  for (int i = 0; i < BTN_COUNT; i++) physOf[i] = (uint8_t)i;
}

static void loadMap() {
  identityMap();
  if (!prefs.begin("wm", true)) return;
  uint8_t buf[BTN_COUNT];
  size_t n = prefs.getBytes("btn", buf, BTN_COUNT);
  prefs.end();
  if (n == BTN_COUNT && validMap(buf)) memcpy(physOf, buf, BTN_COUNT);
}

static void saveMap() {
  if (!prefs.begin("wm", false)) return;
  prefs.putBytes("btn", physOf, BTN_COUNT);
  prefs.end();
}

void buttonsResetMap() {
  identityMap();
  saveMap();
}

void buttonsApplyMap(const uint8_t physOfLogical[BTN_COUNT]) {
  if (!physOfLogical || !validMap(physOfLogical)) return;
  memcpy(physOf, physOfLogical, BTN_COUNT);
  saveMap();
}

void buttonsBegin() {
  for (int i = 0; i < BTN_COUNT; i++) {
    gpio_reset_pin((gpio_num_t)PINS[i]);
    pinMode(PINS[i], INPUT_PULLUP);
    st.down[i] = st.pressed[i] = st.released[i] = st.repeat[i] = false;
    st.heldMs[i] = 0;
    stable[i] = last[i] = rawPressed[i] = false;
    debounceAt[i] = 0;
    lastRepeat[i] = 0;
  }
  lastPoll = millis();
  delay(20);
  if (digitalRead(PIN_BTN_WEAPON) == LOW) {
    delay(1500);
    if (digitalRead(PIN_BTN_WEAPON) == LOW) {
      buttonsResetMap();
    } else {
      loadMap();
    }
  } else {
    loadMap();
  }
}

void buttonsPoll() {
  uint32_t now = millis();
  uint32_t dt = now - lastPoll;
  if (dt > 100) dt = 100;
  lastPoll = now;

  for (int i = 0; i < BTN_COUNT; i++) {
    bool raw = digitalRead(PINS[i]) == LOW;
    if (raw != last[i]) {
      last[i] = raw;
      debounceAt[i] = now;
    }
    bool next = stable[i];
    if ((now - debounceAt[i]) >= 8) next = last[i];
    rawPressed[i] = (!stable[i] && next);
    stable[i] = next;
  }

  for (int i = 0; i < BTN_COUNT; i++) {
    uint8_t p = physOf[i] < BTN_COUNT ? physOf[i] : (uint8_t)i;
    bool on = stable[p];
    st.pressed[i] = (!st.down[i] && on);
    st.released[i] = (st.down[i] && !on);
    st.down[i] = on;
    st.repeat[i] = false;

    if (st.down[i]) {
      uint32_t h = (uint32_t)st.heldMs[i] + dt;
      if (h > 60000) h = 60000;
      st.heldMs[i] = (uint16_t)h;
      if (st.pressed[i]) {
        lastRepeat[i] = now;
      } else if (st.heldMs[i] >= 320 && (now - lastRepeat[i]) >= 180) {
        st.repeat[i] = true;
        lastRepeat[i] = now;
      }
    } else {
      st.heldMs[i] = 0;
    }
  }
}

int buttonsRawPressed() {
  for (int i = 0; i < BTN_COUNT; i++) if (rawPressed[i]) return i;
  return -1;
}

bool buttonsAnyRawDown() {
  for (int i = 0; i < BTN_COUNT; i++) if (stable[i]) return true;
  return false;
}

const Buttons& buttons() { return st; }
bool btnDown(uint8_t i) { return i < BTN_COUNT && st.down[i]; }
bool btnPressed(uint8_t i) { return i < BTN_COUNT && st.pressed[i]; }
bool btnReleased(uint8_t i) { return i < BTN_COUNT && st.released[i]; }
bool btnRepeat(uint8_t i) { return i < BTN_COUNT && st.repeat[i]; }
uint16_t btnHeld(uint8_t i) { return i < BTN_COUNT ? st.heldMs[i] : 0; }
