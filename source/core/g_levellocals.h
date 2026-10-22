#pragma once

// Stand-in for GZDoom's g_levellocals.h, for common code that only reads the world timer.
extern int gametic;

struct FLevelLocals
{
	int& LocalWorldTimer = gametic;
};

extern FLevelLocals* primaryLevel;
