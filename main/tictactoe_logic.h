// main/tictactoe_logic.h — Pure C game model and minimax AI for Tic-Tac-Toe
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TTT_GRID_SIZE  3
#define TTT_CELL_COUNT 9

typedef enum {
    TTT_CELL_EMPTY = 0,
    TTT_CELL_X     = 1,
    TTT_CELL_O     = 2,
} ttt_cell_t;

typedef enum {
    TTT_STATE_READY = 0,   // Initialized / waiting for first move
    TTT_STATE_PLAYING,     // In progress
    TTT_STATE_WIN_X,       // Player X won
    TTT_STATE_WIN_O,       // Player O won
    TTT_STATE_DRAW,        // Game drawn (cat's game)
} ttt_state_t;

typedef enum {
    TTT_MODE_PVE = 0,      // Player (X) vs AI (O)
    TTT_MODE_PVP = 1,      // Player 1 (X) vs Player 2 (O)
} ttt_mode_t;

typedef struct {
    int x_wins;
    int o_wins;
    int draws;
} ttt_score_t;

typedef struct {
    ttt_cell_t board[TTT_CELL_COUNT];
    int cursor;                    // 0..8
    ttt_cell_t current_turn;       // TTT_CELL_X or TTT_CELL_O
    ttt_state_t state;
    ttt_mode_t mode;
    int win_line;                  // -1 if none, 0..7
    int win_cells[3];              // cell indices making up the win
    ttt_score_t score;
    ttt_cell_t starting_turn;      // alternates each round
} ttt_game_t;

void ttt_game_init(ttt_game_t *game);
void ttt_game_start_round(ttt_game_t *game);
void ttt_game_reset_score(ttt_game_t *game);
void ttt_game_toggle_mode(ttt_game_t *game);

bool ttt_game_move_cursor(ttt_game_t *game, int delta);
void ttt_game_set_cursor(ttt_game_t *game, int index);

// Place a move at cursor position for current player.
// Returns true if placement was valid.
bool ttt_game_place_at_cursor(ttt_game_t *game);

// Place a move at a specific cell for current player.
bool ttt_game_make_move(ttt_game_t *game, int cell);

// Evaluate board state without mutating game.
ttt_state_t ttt_evaluate_board(const ttt_cell_t board[TTT_CELL_COUNT], int *out_win_line, int out_win_cells[3]);

// Compute best move for AI using minimax algorithm. Returns cell 0..8, or -1 if full.
int ttt_ai_best_move(const ttt_cell_t board[TTT_CELL_COUNT], ttt_cell_t ai_piece);
