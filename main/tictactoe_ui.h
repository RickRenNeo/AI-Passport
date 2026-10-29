// main/tictactoe_ui.h — Redesigned LVGL user interface for Tic-Tac-Toe
#pragma once

#include "tictactoe_logic.h"
#include "lvgl.h"

// Initialize and build the redesigned Tic-Tac-Toe screen
void ttt_ui_create(void);

// Refresh all UI elements based on game state
void ttt_ui_update(const ttt_game_t *game);

// Update top-right battery level display (-1 if unavailable)
void ttt_ui_update_battery(int soc_percent);

// Set explicit status message (e.g. "AI Thinking...")
void ttt_ui_set_status(const char *msg);
