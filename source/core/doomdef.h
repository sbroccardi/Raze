#pragma once

// Stand-in for GZDoom's doomdef.h, for common code that includes it.
// Common code expects a constant tic rate here. Raze's game code uses GameTicRate instead.
#include "i_time.h"

constexpr int TICRATE = 35;
