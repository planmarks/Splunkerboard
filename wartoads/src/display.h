#pragma once
#include "config.h"

void displayBegin();
bool displayOk();
uint8_t displayAddr();
void displayClear();
void displaySet(int x, int y, bool on);
bool displayGet(int x, int y);
void displayHLine(int x, int y, int w, bool on);
void displayVLine(int x, int y, int h, bool on);
void displayFillRect(int x, int y, int w, int h, bool on);
void displayLine(int x0, int y0, int x1, int y1, bool on);
void displayCircle(int cx, int cy, int r, bool on, bool fill);
void displayChar(int x, int y, char c, bool on);
void displayText(int x, int y, const char* s, bool on);
int displayTextWidth(const char* s);
void displayInvertRect(int x, int y, int w, int h);
void displayBlitOr(const uint8_t* src);
void displayPresent();
uint8_t* displayBuffer();
void displayCenterText(int y, const char* s, bool on);
