// tests/test_tictactoe_logic.c — Host test for Tic-Tac-Toe game logic and minimax AI
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/tictactoe_logic.h"

static void test_initialization(void) {
    ttt_game_t game;
    ttt_game_init(&game);

    assert(game.mode == TTT_MODE_PVE);
    assert(game.current_turn == TTT_CELL_X);
    assert(game.starting_turn == TTT_CELL_X);
    assert(game.state == TTT_STATE_PLAYING);
    assert(game.cursor == 4);
    assert(game.score.x_wins == 0);
    assert(game.score.o_wins == 0);
    assert(game.score.draws == 0);
    assert(game.win_line == -1);

    for (int i = 0; i < TTT_CELL_COUNT; i++) {
        assert(game.board[i] == TTT_CELL_EMPTY);
    }
}

static void test_cursor_navigation(void) {
    ttt_game_t game;
    ttt_game_init(&game);

    assert(game.cursor == 4);
    assert(ttt_game_move_cursor(&game, 1));
    assert(game.cursor == 5);
    assert(ttt_game_move_cursor(&game, 1));
    assert(game.cursor == 6);
    assert(ttt_game_move_cursor(&game, 1));
    assert(game.cursor == 7);
    assert(ttt_game_move_cursor(&game, 1));
    assert(game.cursor == 8);
    assert(ttt_game_move_cursor(&game, 1));
    assert(game.cursor == 0); // Wrap around forward

    assert(ttt_game_move_cursor(&game, -1));
    assert(game.cursor == 8); // Wrap around backward
}

static void test_valid_and_invalid_moves(void) {
    ttt_game_t game;
    ttt_game_init(&game);

    // Place at center (cursor 4)
    assert(ttt_game_place_at_cursor(&game));
    assert(game.board[4] == TTT_CELL_X);
    assert(game.current_turn == TTT_CELL_O);
    assert(game.state == TTT_STATE_PLAYING);

    // Trying to place again at cell 4 should fail
    assert(!ttt_game_place_at_cursor(&game));
    assert(game.board[4] == TTT_CELL_X);
    assert(game.current_turn == TTT_CELL_O);

    // Move cursor and place O
    ttt_game_move_cursor(&game, 1); // cursor 5
    assert(ttt_game_place_at_cursor(&game));
    assert(game.board[5] == TTT_CELL_O);
    assert(game.current_turn == TTT_CELL_X);
}

static void test_row_win(void) {
    ttt_game_t game;
    ttt_game_init(&game);

    // X: 0, O: 3, X: 1, O: 4, X: 2 -> X wins on top row (line 0)
    assert(ttt_game_make_move(&game, 0)); // X
    assert(ttt_game_make_move(&game, 3)); // O
    assert(ttt_game_make_move(&game, 1)); // X
    assert(ttt_game_make_move(&game, 4)); // O
    assert(ttt_game_make_move(&game, 2)); // X

    assert(game.state == TTT_STATE_WIN_X);
    assert(game.win_line == 0);
    assert(game.win_cells[0] == 0 && game.win_cells[1] == 1 && game.win_cells[2] == 2);
    assert(game.score.x_wins == 1);
    assert(game.score.o_wins == 0);
    assert(game.score.draws == 0);

    // No further moves allowed once game is won
    assert(!ttt_game_make_move(&game, 5));
}

static void test_col_win(void) {
    ttt_game_t game;
    ttt_game_init(&game);

    // X: 0, O: 1, X: 3, O: 4, X: 6 -> X wins on Col 0 (line 3)
    assert(ttt_game_make_move(&game, 0)); // X
    assert(ttt_game_make_move(&game, 1)); // O
    assert(ttt_game_make_move(&game, 3)); // X
    assert(ttt_game_make_move(&game, 4)); // O
    assert(ttt_game_make_move(&game, 6)); // X

    assert(game.state == TTT_STATE_WIN_X);
    assert(game.win_line == 3);
    assert(game.win_cells[0] == 0 && game.win_cells[1] == 3 && game.win_cells[2] == 6);
    assert(game.score.x_wins == 1);
}

static void test_diagonal_win(void) {
    ttt_game_t game;
    ttt_game_init(&game);

    // X: 0, O: 1, X: 4, O: 2, X: 8 -> X wins on diag (line 6)
    assert(ttt_game_make_move(&game, 0)); // X
    assert(ttt_game_make_move(&game, 1)); // O
    assert(ttt_game_make_move(&game, 4)); // X
    assert(ttt_game_make_move(&game, 2)); // O
    assert(ttt_game_make_move(&game, 8)); // X

    assert(game.state == TTT_STATE_WIN_X);
    assert(game.win_line == 6);
    assert(game.win_cells[0] == 0 && game.win_cells[1] == 4 && game.win_cells[2] == 8);
}

static void test_draw_game(void) {
    ttt_game_t game;
    ttt_game_init(&game);

    /*
       X | O | X
       X | O | O
       O | X | X
       Cells:
       0:X, 1:O, 2:X
       3:X, 4:O, 5:O
       6:O, 7:X, 8:X
    */
    assert(ttt_game_make_move(&game, 0)); // X
    assert(ttt_game_make_move(&game, 1)); // O
    assert(ttt_game_make_move(&game, 2)); // X
    assert(ttt_game_make_move(&game, 4)); // O
    assert(ttt_game_make_move(&game, 3)); // X
    assert(ttt_game_make_move(&game, 5)); // O
    assert(ttt_game_make_move(&game, 7)); // X
    assert(ttt_game_make_move(&game, 6)); // O
    assert(ttt_game_make_move(&game, 8)); // X

    assert(game.state == TTT_STATE_DRAW);
    assert(game.win_line == -1);
    assert(game.score.draws == 1);
}

static void test_ai_take_winning_move(void) {
    /*
       Board:
       O | O | . (cell 2 empty, AI should pick 2 to win!)
       X | X | .
       . | . | .
    */
    ttt_cell_t board[9] = {
        TTT_CELL_O, TTT_CELL_O, TTT_CELL_EMPTY,
        TTT_CELL_X, TTT_CELL_X, TTT_CELL_EMPTY,
        TTT_CELL_EMPTY, TTT_CELL_EMPTY, TTT_CELL_EMPTY
    };
    int move = ttt_ai_best_move(board, TTT_CELL_O);
    assert(move == 2);
}

static void test_ai_block_opponent_win(void) {
    /*
       Board:
       X | X | . (cell 2 empty, AI O must block 2!)
       O | . | .
       . | . | .
    */
    ttt_cell_t board[9] = {
        TTT_CELL_X, TTT_CELL_X, TTT_CELL_EMPTY,
        TTT_CELL_O, TTT_CELL_EMPTY, TTT_CELL_EMPTY,
        TTT_CELL_EMPTY, TTT_CELL_EMPTY, TTT_CELL_EMPTY
    };
    int move = ttt_ai_best_move(board, TTT_CELL_O);
    assert(move == 2);
}

static void test_ai_empty_board(void) {
    ttt_cell_t board[9] = {0};
    int move = ttt_ai_best_move(board, TTT_CELL_O);
    // Center is preferred
    assert(move == 4);
}

static void test_ai_self_play_always_draws(void) {
    // Two optimal minimax agents playing against each other must always end in a draw
    ttt_cell_t board[9] = {0};
    ttt_cell_t turn = TTT_CELL_X;

    for (int step = 0; step < 9; step++) {
        int move = ttt_ai_best_move(board, turn);
        assert(move >= 0 && move < 9);
        assert(board[move] == TTT_CELL_EMPTY);
        board[move] = turn;

        int win_line;
        int win_cells[3];
        ttt_state_t st = ttt_evaluate_board(board, &win_line, win_cells);
        if (st != TTT_STATE_PLAYING) {
            assert(st == TTT_STATE_DRAW);
            break;
        }
        turn = (turn == TTT_CELL_X) ? TTT_CELL_O : TTT_CELL_X;
    }
}

static void test_mode_and_reset(void) {
    ttt_game_t game;
    ttt_game_init(&game);
    game.score.x_wins = 5;
    game.score.o_wins = 3;

    ttt_game_toggle_mode(&game);
    assert(game.mode == TTT_MODE_PVP);
    assert(game.score.x_wins == 0);
    assert(game.score.o_wins == 0);

    ttt_game_toggle_mode(&game);
    assert(game.mode == TTT_MODE_PVE);
}

int main(void) {
    test_initialization();
    test_cursor_navigation();
    test_valid_and_invalid_moves();
    test_row_win();
    test_col_win();
    test_diagonal_win();
    test_draw_game();
    test_ai_take_winning_move();
    test_ai_block_opponent_win();
    test_ai_empty_board();
    test_ai_self_play_always_draws();
    test_mode_and_reset();

    printf("Tic-Tac-Toe logic host tests: PASS\n");
    return 0;
}
