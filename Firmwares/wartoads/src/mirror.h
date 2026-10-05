#pragma once
#include "config.h"

#if WM_MIRROR
void mirrorPoll();
void mirrorPush();
bool mirrorActive();
#else
inline void mirrorPoll() {}
inline void mirrorPush() {}
inline bool mirrorActive() { return false; }
#endif
