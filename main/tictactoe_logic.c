// main/tictactoe_logic.c — Pure C game model and minimax AI for Tic-Tac-Toe
#include "tictactoe_logic.h"
#include <string.h>

static const int WINNING_COMBOS[8][3] = {
    {0, 1, 2}, // Row 0
    {3, 4, 5}, // Row 1
    {6, 7, 8}, // Row 2
    {0, 3, 6}, // Col 0
    {1, 4, 7}, // Col 1
    {2, 5, 8}, // Col 2
    {0, 4, 8}, // Diagonal top-left to bottom-right
    {2, 4, 6}, // Diagonal top-right to bottom-left
};

// Strategic preference weight: Center (4) > Corners (0,2,6,8) > Edges (1,3,5,7)
static const int POSITION_WEIGHT[9] = {
    3, 1, 3,
    1, 4, 1,
    3, 1, 3
};

void ttt_game_init(ttt_game_t *game) {
    if (!game) return;
    memset(game, 0, sizeof(*game));
    game->mode = TTT_MODE_PVE;
    game->starting_turn = TTT_CELL_X;
    ttt_game_start_round(game);
}

void ttt_game_start_round(ttt_game_t *game) {
    if (!game) return;
    for (int i = 0; i < TTT_CELL_COUNT; i++) {
        game->board[i] = TTT_CELL_EMPTY;
    }
    game->cursor = 4; // Start cursor at center
    game->current_turn = game->starting_turn;
    game->state = TTT_STATE_PLAYING;
    game->win_line = -1;
    game->win_cells[0] = -1;
    game->win_cells[1] = -1;
    game->win_cells[2] = -1;
}

void ttt_game_reset_score(ttt_game_t *game) {
    if (!game) return;
    game->score.x_wins = 0;
    game->score.o_wins = 0;
    game->score.draws = 0;
}

void ttt_game_toggle_mode(ttt_game_t *game) {
    if (!game) return;
    game->mode = (game->mode == TTT_MODE_PVE) ? TTT_MODE_PVP : TTT_MODE_PVE;
    ttt_game_reset_score(game);
    game->starting_turn = TTT_CELL_X;
    ttt_game_start_round(game);
}

bool ttt_game_move_cursor(ttt_game_t *game, int delta) {
    if (!game) return false;
    int next = (game->cursor + delta) % TTT_CELL_COUNT;
    if (next < 0) next += TTT_CELL_COUNT;
    if (next != game->cursor) {
        game->cursor = next;
        return true;
    }
    return false;
}

void ttt_game_set_cursor(ttt_game_t *game, int index) {
    if (!game) return;
    if (index >= 0 && index < TTT_CELL_COUNT) {
        game->cursor = index;
    }
}

ttt_state_t ttt_evaluate_board(const ttt_cell_t board[TTT_CELL_COUNT], int *out_win_line, int out_win_cells[3]) {
    if (out_win_line) *out_win_line = -1;
    if (out_win_cells) {
        out_win_cells[0] = -1;
        out_win_cells[1] = -1;
        out_win_cells[2] = -1;
    }

    // Check 8 winning combinations
    for (int i = 0; i < 8; i++) {
        int a = WINNING_COMBOS[i][0];
        int b = WINNING_COMBOS[i][1];
        int c = WINNING_COMBOS[i][2];
        if (board[a] != TTT_CELL_EMPTY &&
            board[a] == board[b] &&
            board[b] == board[c]) {
            if (out_win_line) *out_win_line = i;
            if (out_win_cells) {
                out_win_cells[0] = a;
                out_win_cells[1] = b;
                out_win_cells[2] = c;
            }
            return (board[a] == TTT_CELL_X) ? TTT_STATE_WIN_X : TTT_STATE_WIN_O;
        }
    }

    // Check if any empty cell remains
    for (int i = 0; i < TTT_CELL_COUNT; i++) {
        if (board[i] == TTT_CELL_EMPTY) {
            return TTT_STATE_PLAYING;
        }
    }

    return TTT_STATE_DRAW;
}

bool ttt_game_make_move(ttt_game_t *game, int cell) {
    if (!game || cell < 0 || cell >= TTT_CELL_COUNT) return false;
    if (game->state != TTT_STATE_PLAYING) return false;
    if (game->board[cell] != TTT_CELL_EMPTY) return false;

    game->board[cell] = game->current_turn;

    // Check board state after move
    ttt_state_t outcome = ttt_evaluate_board(game->board, &game->win_line, game->win_cells);
    game->state = outcome;

    if (outcome == TTT_STATE_WIN_X) {
        game->score.x_wins++;
        // Switch starting player for next round
        game->starting_turn = (game->starting_turn == TTT_CELL_X) ? TTT_CELL_O : TTT_CELL_X;
    } else if (outcome == TTT_STATE_WIN_O) {
        game->score.o_wins++;
        game->starting_turn = (game->starting_turn == TTT_CELL_X) ? TTT_CELL_O : TTT_CELL_X;
    } else if (outcome == TTT_STATE_DRAW) {
        game->score.draws++;
        game->starting_turn = (game->starting_turn == TTT_CELL_X) ? TTT_CELL_O : TTT_CELL_X;
    } else {
        // Toggle turn
        game->current_turn = (game->current_turn == TTT_CELL_X) ? TTT_CELL_O : TTT_CELL_X;
    }

    return true;
}

bool ttt_game_place_at_cursor(ttt_game_t *game) {
    if (!game) return false;
    return ttt_game_make_move(game, game->cursor);
}

// Minimax algorithm for optimal AI
static int minimax(ttt_cell_t b[TTT_CELL_COUNT], int depth, bool is_max, ttt_cell_t ai_piece, ttt_cell_t human_piece) {
    int dummy_line;
    int dummy_cells[3];
    ttt_state_t st = ttt_evaluate_board(b, &dummy_line, dummy_cells);

    if (st == TTT_STATE_WIN_X) {
        return (ai_piece == TTT_CELL_X) ? (10 - depth) : (depth - 10);
    }
    if (st == TTT_STATE_WIN_O) {
        return (ai_piece == TTT_CELL_O) ? (10 - depth) : (depth - 10);
    }
    if (st == TTT_STATE_DRAW) {
        return 0;
    }

    if (is_max) {
        int best = -100;
        for (int i = 0; i < TTT_CELL_COUNT; i++) {
            if (b[i] == TTT_CELL_EMPTY) {
                b[i] = ai_piece;
                int score = minimax(b, depth + 1, false, ai_piece, human_piece);
                b[i] = TTT_CELL_EMPTY;
                if (score > best) best = score;
            }
        }
        return best;
    } else {
        int best = 100;
        for (int i = 0; i < TTT_CELL_COUNT; i++) {
            if (b[i] == TTT_CELL_EMPTY) {
                b[i] = human_piece;
                int score = minimax(b, depth + 1, true, ai_piece, human_piece);
                b[i] = TTT_CELL_EMPTY;
                if (score < best) best = score;
            }
        }
        return best;
    }
}

int ttt_ai_best_move(const ttt_cell_t board[TTT_CELL_COUNT], ttt_cell_t ai_piece) {
    ttt_cell_t human_piece = (ai_piece == TTT_CELL_X) ? TTT_CELL_O : TTT_CELL_X;
    ttt_cell_t temp[TTT_CELL_COUNT];
    memcpy(temp, board, sizeof(temp));

    int best_score = -1000;
    int best_move = -1;
    int best_weight = -1;

    for (int i = 0; i < TTT_CELL_COUNT; i++) {
        if (temp[i] == TTT_CELL_EMPTY) {
            temp[i] = ai_piece;
            int score = minimax(temp, 0, false, ai_piece, human_piece);
            temp[i] = TTT_CELL_EMPTY;

            int weight = POSITION_WEIGHT[i];
            if (score > best_score || (score == best_score && weight > best_weight)) {
                best_score = score;
                best_move = i;
                best_weight = weight;
            }
        }
    }

    return best_move;
}
