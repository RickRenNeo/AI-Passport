// main/tictactoe_audio.h — Audio sound effects for Tic-Tac-Toe
#pragma once

#include <stdbool.h>

typedef enum {
    TTT_SFX_MOVE = 0,
    TTT_SFX_PLACE,
    TTT_SFX_INVALID,
    TTT_SFX_WIN,
    TTT_SFX_LOSE,
    TTT_SFX_DRAW,
} ttt_sfx_t;

// Initialize audio worker task and configure ES8311 codec.
// Safe no-op / graceful degradation if audio hardware is missing.
void ttt_audio_init(void);

// Queue a sound effect non-blockingly from button or game event context.
void ttt_audio_play(ttt_sfx_t sfx);
