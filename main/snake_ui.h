#pragma once

#include "snake_logic.h"
#include <stdint.h>

/* Initialize and create Snake UI widgets on screen */
void snake_ui_create(void);

/* Update UI display with current game state and battery percentage */
void snake_ui_update(const snake_game_t *game, int battery_soc);

/* Free any resources on exit */
void snake_ui_destroy(void);
