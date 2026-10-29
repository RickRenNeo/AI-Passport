#pragma once

#include <stdbool.h>

typedef enum {
    SNAKE_SFX_START = 0,
    SNAKE_SFX_TURN,
    SNAKE_SFX_EAT,
    SNAKE_SFX_BONUS,
    SNAKE_SFX_DIE,
    SNAKE_SFX_PAUSE,
} snake_sfx_t;

void snake_audio_init(void);
void snake_audio_play(snake_sfx_t sfx);
