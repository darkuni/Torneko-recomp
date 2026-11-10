// World of Dragon Warrior: Torneko - The Last Hope (NTSC-U, SLUS-01181) - game-specific constants.
#pragma once
#define GAME_NAME "Torneko: The Last Hope"   // window title, message boxes
#define GAME_INI "torneko.ini"
#define GAME_BIN "torneko.exe"
#define GAME_EXE_NAME "SLUS_011.81"
#define GAME_SERIAL "SLUS-01181"
#define VSYNC_WAIT_FN 0x80097074u       // libetc VSync wait loop (replaced: see hle_vsync_wait)
static const unsigned GAME_EXE_SIZE = 1122304u;
static const unsigned GAME_EXE_PC = 0x8001e404u;

// SYSTEM.CNF: TCB = 4, EVENT = 16, STACK = 801fff00
static const int SYS_TCBS = 4;
static const int SYS_EVENTS = 16;
static const unsigned SYS_STACK = 0x801FFF00u;
