// main/tictactoe_ui.c — Redesigned LVGL user interface for Tic-Tac-Toe
#include "tictactoe_ui.h"
#include <stdio.h>

#define COLOR_BG          0x181825  // Deep midnight
#define COLOR_PANEL       0x1E1E2E  // Dark surface
#define COLOR_CELL_DEF    0x313244  // Cell default
#define COLOR_BORDER_DEF  0x45475A  // Normal border
#define COLOR_FOCUS       0xF9E2AF  // Cursor highlight gold
#define COLOR_WIN_BG      0xA6E3A1  // Winning cell green
#define COLOR_WIN_TEXT    0x181825  // Winning text dark
#define COLOR_X           0x89B4FA  // Ice blue
#define COLOR_O           0xF38BA8  // Coral pink
#define COLOR_TEXT        0xCDD6F4  // Crisp light text
#define COLOR_MUTED       0x9399B2  // Secondary text

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_mode_label = NULL;
static lv_obj_t *s_battery_label = NULL;
static lv_obj_t *s_score_x_label = NULL;
static lv_obj_t *s_score_draw_label = NULL;
static lv_obj_t *s_score_o_label = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_cell_boxes[TTT_CELL_COUNT];
static lv_obj_t *s_cell_labels[TTT_CELL_COUNT];

static lv_obj_t *create_card(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg_col, uint32_t border_col) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border_col), 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg_col), 0);
    return obj;
}

void ttt_ui_create(void) {
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    // 1. Header Bar: Title, Mode badge, Battery
    lv_obj_t *title_label = lv_label_create(s_screen);
    lv_label_set_text(title_label, "TIC-TAC-TOE");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_pos(title_label, 14, 8);

    s_mode_label = lv_label_create(s_screen);
    lv_label_set_text(s_mode_label, "VS AI");
    lv_obj_set_style_text_font(s_mode_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_mode_label, lv_color_hex(COLOR_X), 0);
    lv_obj_set_pos(s_mode_label, 122, 8);

    // Battery badge container in top-right
    lv_obj_t *bat_card = create_card(s_screen, 184, 5, 44, 20, COLOR_PANEL, COLOR_BORDER_DEF);
    lv_obj_set_style_radius(bat_card, 4, 0);
    s_battery_label = lv_label_create(bat_card);
    lv_label_set_text(s_battery_label, "--%");
    lv_obj_set_style_text_font(s_battery_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_battery_label, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_center(s_battery_label);

    // 2. Scoreboard (Width: 216, centered at x=12, y=28, height=44)
    lv_obj_t *score_card = create_card(s_screen, 12, 28, 216, 44, COLOR_PANEL, COLOR_BORDER_DEF);

    // Column 1: X (Player)
    lv_obj_t *x_title = lv_label_create(score_card);
    lv_label_set_text(x_title, "YOU(X)");
    lv_obj_set_style_text_font(x_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(x_title, lv_color_hex(COLOR_X), 0);
    lv_obj_set_pos(x_title, 14, 2);

    s_score_x_label = lv_label_create(score_card);
    lv_label_set_text(s_score_x_label, "0");
    lv_obj_set_style_text_font(s_score_x_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_score_x_label, lv_color_hex(COLOR_X), 0);
    lv_obj_set_pos(s_score_x_label, 26, 18);

    // Column 2: TIE
    lv_obj_t *tie_title = lv_label_create(score_card);
    lv_label_set_text(tie_title, "TIE");
    lv_obj_set_style_text_font(tie_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tie_title, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_pos(tie_title, 96, 2);

    s_score_draw_label = lv_label_create(score_card);
    lv_label_set_text(s_score_draw_label, "0");
    lv_obj_set_style_text_font(s_score_draw_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_score_draw_label, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_pos(s_score_draw_label, 102, 18);

    // Column 3: O (AI / Player 2)
    lv_obj_t *o_title = lv_label_create(score_card);
    lv_label_set_text(o_title, "AI(O)");
    lv_obj_set_style_text_font(o_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(o_title, lv_color_hex(COLOR_O), 0);
    lv_obj_set_pos(o_title, 160, 2);

    s_score_o_label = lv_label_create(score_card);
    lv_label_set_text(s_score_o_label, "0");
    lv_obj_set_style_text_font(s_score_o_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_score_o_label, lv_color_hex(COLOR_O), 0);
    lv_obj_set_pos(s_score_o_label, 172, 18);

    // 3. 3x3 Board Cells
    // Cell size: 52x52, gap: 5px. Origin: x=37, y=78
    for (int i = 0; i < TTT_CELL_COUNT; i++) {
        int col = i % 3;
        int row = i / 3;
        int x = 37 + col * 57;
        int y = 78 + row * 57;

        s_cell_boxes[i] = create_card(s_screen, x, y, 52, 52, COLOR_CELL_DEF, COLOR_BORDER_DEF);
        lv_obj_set_style_radius(s_cell_boxes[i], 10, 0);

        s_cell_labels[i] = lv_label_create(s_cell_boxes[i]);
        lv_label_set_text(s_cell_labels[i], "");
        lv_obj_set_style_text_font(s_cell_labels[i], &lv_font_montserrat_20, 0);
        lv_obj_center(s_cell_labels[i]);
    }

    // 4. Status Message Banner
    s_status_label = lv_label_create(s_screen);
    lv_label_set_text(s_status_label, "YOUR TURN (X)");
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_FOCUS), 0);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_status_label, 10, 255);
    lv_obj_set_size(s_status_label, 220, 18);

    // 5. Footer Navigation Hints (compact and centered)
    lv_obj_t *hint_label = lv_label_create(s_screen);
    lv_label_set_text(hint_label, "UP/DN:Move  OK:Play  Hold:Mode");
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(hint_label, 0, 278);
    lv_obj_set_size(hint_label, 240, 18);

    lv_screen_load(s_screen);
}

void ttt_ui_update_battery(int soc_percent) {
    if (!s_battery_label) return;
    char buf[16];
    if (soc_percent >= 0) {
        snprintf(buf, sizeof(buf), "%d%%", soc_percent);
        lv_label_set_text(s_battery_label, buf);
        lv_obj_set_style_text_color(s_battery_label, lv_color_hex(COLOR_TEXT), 0);
    } else {
        lv_label_set_text(s_battery_label, "--%");
        lv_obj_set_style_text_color(s_battery_label, lv_color_hex(COLOR_MUTED), 0);
    }
}

void ttt_ui_set_status(const char *msg) {
    if (!s_status_label || !msg) return;
    lv_label_set_text(s_status_label, msg);
}

void ttt_ui_update(const ttt_game_t *game) {
    if (!game || !s_screen) return;

    // Update Mode text
    if (s_mode_label) {
        lv_label_set_text(s_mode_label, (game->mode == TTT_MODE_PVE) ? "VS AI" : "2P PVP");
    }

    // Update Scores
    if (s_score_x_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", game->score.x_wins);
        lv_label_set_text(s_score_x_label, buf);
    }
    if (s_score_draw_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", game->score.draws);
        lv_label_set_text(s_score_draw_label, buf);
    }
    if (s_score_o_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", game->score.o_wins);
        lv_label_set_text(s_score_o_label, buf);
    }

    // Update 3x3 Cells
    for (int i = 0; i < TTT_CELL_COUNT; i++) {
        lv_obj_t *box = s_cell_boxes[i];
        lv_obj_t *lbl = s_cell_labels[i];
        bool is_focused = (i == game->cursor);
        bool is_win_cell = false;

        if (game->win_line >= 0) {
            for (int k = 0; k < 3; k++) {
                if (game->win_cells[k] == i) {
                    is_win_cell = true;
                    break;
                }
            }
        }

        // Cell content
        if (game->board[i] == TTT_CELL_X) {
            lv_label_set_text(lbl, "X");
            lv_obj_set_style_text_color(lbl, is_win_cell ? lv_color_hex(COLOR_WIN_TEXT) : lv_color_hex(COLOR_X), 0);
        } else if (game->board[i] == TTT_CELL_O) {
            lv_label_set_text(lbl, "O");
            lv_obj_set_style_text_color(lbl, is_win_cell ? lv_color_hex(COLOR_WIN_TEXT) : lv_color_hex(COLOR_O), 0);
        } else {
            // Empty cell: show dot if cursor is on it
            lv_label_set_text(lbl, is_focused ? "." : "");
            lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_FOCUS), 0);
        }

        // Cell styling
        if (is_win_cell) {
            lv_obj_set_style_bg_color(box, lv_color_hex(COLOR_WIN_BG), 0);
            lv_obj_set_style_border_color(box, lv_color_hex(COLOR_WIN_BG), 0);
            lv_obj_set_style_border_width(box, 3, 0);
        } else if (is_focused) {
            lv_obj_set_style_bg_color(box, lv_color_hex(COLOR_PANEL), 0);
            lv_obj_set_style_border_color(box, lv_color_hex(COLOR_FOCUS), 0);
            lv_obj_set_style_border_width(box, 3, 0);
        } else {
            lv_obj_set_style_bg_color(box, lv_color_hex(COLOR_CELL_DEF), 0);
            lv_obj_set_style_border_color(box, lv_color_hex(COLOR_BORDER_DEF), 0);
            lv_obj_set_style_border_width(box, 1, 0);
        }
    }

    // Update Status Label
    if (s_status_label) {
        if (game->state == TTT_STATE_WIN_X) {
            lv_label_set_text(s_status_label, (game->mode == TTT_MODE_PVE) ? "YOU WIN! (Click OK)" : "PLAYER X WINS! (OK)");
            lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_WIN_BG), 0);
        } else if (game->state == TTT_STATE_WIN_O) {
            lv_label_set_text(s_status_label, (game->mode == TTT_MODE_PVE) ? "AI WINS! (Click OK)" : "PLAYER O WINS! (OK)");
            lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_O), 0);
        } else if (game->state == TTT_STATE_DRAW) {
            lv_label_set_text(s_status_label, "DRAW GAME! (Click OK)");
            lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_FOCUS), 0);
        } else {
            if (game->mode == TTT_MODE_PVE) {
                if (game->current_turn == TTT_CELL_X) {
                    lv_label_set_text(s_status_label, "YOUR TURN (X)");
                    lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_TEXT), 0);
                } else {
                    lv_label_set_text(s_status_label, "AI THINKING...");
                    lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_O), 0);
                }
            } else {
                if (game->current_turn == TTT_CELL_X) {
                    lv_label_set_text(s_status_label, "PLAYER X TURN");
                    lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_X), 0);
                } else {
                    lv_label_set_text(s_status_label, "PLAYER O TURN");
                    lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_O), 0);
                }
            }
        }
    }
}
