#include "snake_ui.h"
#include "esp_log.h"
#include "lvgl.h"
#include <stdio.h>

LV_FONT_DECLARE(lv_font_cn_16);

#define BOARD_PIXEL_W 200
#define BOARD_PIXEL_H 240
#define CELL_PX       10

#define COLOR_PAL_BG        0x0B0F19
#define COLOR_PAL_GRID      0x141C2B
#define COLOR_PAL_HEAD      0x00E676
#define COLOR_PAL_EYE       0xFFFFFF
#define COLOR_PAL_PUPIL     0x000000
#define COLOR_PAL_BODY1     0x00C853
#define COLOR_PAL_BODY2     0x009624
#define COLOR_PAL_FOOD      0xFF1744
#define COLOR_PAL_LEAF      0x76FF03
#define COLOR_PAL_BONUS     0xFFD600
#define COLOR_PAL_GLINT     0xFFFFFF
#define COLOR_PAL_DEAD      0x546E7A
#define COLOR_PAL_ACCENT    0x00E5FF

static const uint32_t PALETTE[] = {
    COLOR_PAL_BG,       // 0: Board bg
    COLOR_PAL_GRID,     // 1: Grid dot
    COLOR_PAL_HEAD,     // 2: Snake head
    COLOR_PAL_EYE,      // 3: Eye sclera
    COLOR_PAL_PUPIL,    // 4: Eye pupil
    COLOR_PAL_BODY1,    // 5: Body primary
    COLOR_PAL_BODY2,    // 6: Body secondary
    COLOR_PAL_FOOD,     // 7: Normal food
    COLOR_PAL_LEAF,     // 8: Food leaf
    COLOR_PAL_BONUS,    // 9: Bonus gold food
    COLOR_PAL_GLINT,    // 10: Bonus core glint
    COLOR_PAL_DEAD,     // 11: Dead snake
    COLOR_PAL_ACCENT,   // 12: Accent border
};
#define PALETTE_SIZE (sizeof(PALETTE) / sizeof(PALETTE[0]))

LV_DRAW_BUF_DEFINE_STATIC(s_board_buf, BOARD_PIXEL_W, BOARD_PIXEL_H, LV_COLOR_FORMAT_I4);

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_board_canvas = NULL;
static lv_obj_t *s_score_val = NULL;
static lv_obj_t *s_hi_val = NULL;
static lv_obj_t *s_battery_val = NULL;

static lv_obj_t *s_modal = NULL;
static lv_obj_t *s_modal_title = NULL;
static lv_obj_t *s_modal_sub = NULL;
static lv_obj_t *s_modal_extra = NULL;
static lv_obj_t *s_modal_prompt = NULL;

static void canvas_px(lv_obj_t *canvas, int x, int y, uint8_t color) {
    if (x < 0 || x >= BOARD_PIXEL_W || y < 0 || y >= BOARD_PIXEL_H) return;
    lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(canvas);
    uint8_t *data = lv_draw_buf_goto_xy(draw_buf, x, y);
    if (!data) return;
    uint8_t shift = (uint8_t)(4 - 4 * (x & 1));
    *data = (uint8_t)((*data & ~(0x0FU << shift)) | ((color & 0x0FU) << shift));
}

static void canvas_fill_rect(lv_obj_t *canvas, int x0, int y0, int w, int h, uint8_t color) {
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            canvas_px(canvas, x, y, color);
        }
    }
}

static void render_cell_box(lv_obj_t *canvas, int8_t gx, int8_t gy, uint8_t color) {
    int cx = gx * CELL_PX;
    int cy = gy * CELL_PX;
    canvas_fill_rect(canvas, cx + 2, cy + 1, 6, 8, color);
    canvas_fill_rect(canvas, cx + 1, cy + 2, 8, 6, color);
}

static void render_snake_head(lv_obj_t *canvas, const snake_game_t *game) {
    int8_t gx = game->body[0].x;
    int8_t gy = game->body[0].y;
    int cx = gx * CELL_PX;
    int cy = gy * CELL_PX;

    bool dead = (game->state == SNAKE_STATE_GAME_OVER);
    uint8_t head_col = dead ? 11 : 2;

    canvas_fill_rect(canvas, cx + 2, cy + 1, 6, 8, head_col);
    canvas_fill_rect(canvas, cx + 1, cy + 2, 8, 6, head_col);

    if (!dead) {
        int e1x = 0, e1y = 0, e2x = 0, e2y = 0;
        int p1x = 0, p1y = 0, p2x = 0, p2y = 0;
        switch (game->dir) {
        case SNAKE_DIR_UP:
            e1x = cx + 2; e1y = cy + 2; e2x = cx + 6; e2y = cy + 2;
            p1x = cx + 2; p1y = cy + 1; p2x = cx + 6; p2y = cy + 1;
            break;
        case SNAKE_DIR_DOWN:
            e1x = cx + 2; e1y = cy + 6; e2x = cx + 6; e2y = cy + 6;
            p1x = cx + 2; p1y = cy + 7; p2x = cx + 6; p2y = cy + 7;
            break;
        case SNAKE_DIR_LEFT:
            e1x = cx + 2; e1y = cy + 2; e2x = cx + 2; e2y = cy + 6;
            p1x = cx + 1; p1y = cy + 2; p2x = cx + 1; p2y = cy + 6;
            break;
        case SNAKE_DIR_RIGHT:
            e1x = cx + 6; e1y = cy + 2; e2x = cx + 6; e2y = cy + 6;
            p1x = cx + 7; p1y = cy + 2; p2x = cx + 7; p2y = cy + 6;
            break;
        }

        canvas_px(canvas, e1x, e1y, 3);
        canvas_px(canvas, e1x + 1, e1y, 3);
        canvas_px(canvas, e1x + 1, e1y + 1, 3);
        canvas_px(canvas, e1x, e1y + 1, 3);

        canvas_px(canvas, e2x, e2y, 3);
        canvas_px(canvas, e2x + 1, e2y, 3);
        canvas_px(canvas, e2x + 1, e2y + 1, 3);
        canvas_px(canvas, e2x, e2y + 1, 3);

        canvas_px(canvas, p1x, p1y, 4);
        canvas_px(canvas, p2x, p2y, 4);
    } else {
        canvas_px(canvas, cx + 2, cy + 2, 4);
        canvas_px(canvas, cx + 4, cy + 4, 4);
        canvas_px(canvas, cx + 4, cy + 2, 4);
        canvas_px(canvas, cx + 2, cy + 4, 4);

        canvas_px(canvas, cx + 6, cy + 2, 4);
        canvas_px(canvas, cx + 8, cy + 4, 4);
        canvas_px(canvas, cx + 8, cy + 2, 4);
        canvas_px(canvas, cx + 6, cy + 4, 4);
    }
}

static void render_food(lv_obj_t *canvas, int8_t gx, int8_t gy, bool is_bonus) {
    int cx = gx * CELL_PX;
    int cy = gy * CELL_PX;

    if (!is_bonus) {
        canvas_fill_rect(canvas, cx + 3, cy + 3, 5, 5, 7);
        canvas_fill_rect(canvas, cx + 2, cy + 4, 7, 3, 7);
        canvas_px(canvas, cx + 5, cy + 1, 8);
        canvas_px(canvas, cx + 6, cy + 1, 8);
    } else {
        canvas_fill_rect(canvas, cx + 2, cy + 2, 6, 6, 9);
        canvas_fill_rect(canvas, cx + 4, cy + 1, 2, 8, 9);
        canvas_fill_rect(canvas, cx + 1, cy + 4, 8, 2, 9);
        canvas_px(canvas, cx + 3, cy + 3, 10);
        canvas_px(canvas, cx + 4, cy + 3, 10);
    }
}

static void render_board(const snake_game_t *game) {
    if (!s_board_canvas) return;

    canvas_fill_rect(s_board_canvas, 0, 0, BOARD_PIXEL_W, BOARD_PIXEL_H, 0);

    for (int y = 0; y < SNAKE_GRID_ROWS; y++) {
        for (int x = 0; x < SNAKE_GRID_COLS; x++) {
            canvas_px(s_board_canvas, x * CELL_PX + 4, y * CELL_PX + 4, 1);
        }
    }

    if (game->food.type == SNAKE_FOOD_NORMAL) {
        render_food(s_board_canvas, game->food.pos.x, game->food.pos.y, false);
    }

    if (game->bonus.type == SNAKE_FOOD_BONUS) {
        render_food(s_board_canvas, game->bonus.pos.x, game->bonus.pos.y, true);
    }

    bool dead = (game->state == SNAKE_STATE_GAME_OVER);
    for (int i = game->length - 1; i > 0; i--) {
        uint8_t col = dead ? 11 : ((i % 2 == 0) ? 5 : 6);
        render_cell_box(s_board_canvas, game->body[i].x, game->body[i].y, col);
    }

    render_snake_head(s_board_canvas, game);

    lv_obj_invalidate(s_board_canvas);
}

void snake_ui_create(void) {
    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, 240, 320);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x070A12), 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    // ==========================================
    // Top Status Header (y: 2, h: 38)
    // ==========================================
    lv_obj_t *hdr = lv_obj_create(s_scr);
    lv_obj_set_pos(hdr, 10, 2);
    lv_obj_set_size(hdr, 220, 38);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    // 得分 / Score
    lv_obj_t *sc_lbl = lv_label_create(hdr);
    lv_obj_set_pos(sc_lbl, 0, 0);
    lv_label_set_text(sc_lbl, "得分");
    lv_obj_set_style_text_font(sc_lbl, &lv_font_cn_16, 0);
    lv_obj_set_style_text_color(sc_lbl, lv_color_hex(0x7F92AA), 0);

    s_score_val = lv_label_create(hdr);
    lv_obj_set_pos(s_score_val, 0, 18);
    lv_label_set_text(s_score_val, "00000");
    lv_obj_set_style_text_font(s_score_val, &lv_font_cn_16, 0);
    lv_obj_set_style_text_color(s_score_val, lv_color_hex(0x00E5FF), 0);

    // 最高 / High Score
    lv_obj_t *hi_lbl = lv_label_create(hdr);
    lv_obj_set_pos(hi_lbl, 76, 0);
    lv_label_set_text(hi_lbl, "最高");
    lv_obj_set_style_text_font(hi_lbl, &lv_font_cn_16, 0);
    lv_obj_set_style_text_color(hi_lbl, lv_color_hex(0x7F92AA), 0);

    s_hi_val = lv_label_create(hdr);
    lv_obj_set_pos(s_hi_val, 76, 18);
    lv_label_set_text(s_hi_val, "00000");
    lv_obj_set_style_text_font(s_hi_val, &lv_font_cn_16, 0);
    lv_obj_set_style_text_color(s_hi_val, lv_color_hex(0xFFD600), 0);

    // 电量 / Battery (Top Right corner per repository rules)
    s_battery_val = lv_label_create(hdr);
    lv_obj_set_pos(s_battery_val, 150, 8);
    lv_label_set_text(s_battery_val, "电量 --");
    lv_obj_set_style_text_font(s_battery_val, &lv_font_cn_16, 0);
    lv_obj_set_style_text_color(s_battery_val, lv_color_hex(0x00E676), 0);

    // ==========================================
    // Playfield Canvas Border & Canvas (y: 42, 200x240)
    // ==========================================
    lv_obj_t *board_frame = lv_obj_create(s_scr);
    lv_obj_set_pos(board_frame, 18, 42);
    lv_obj_set_size(board_frame, 204, 244);
    lv_obj_set_style_bg_color(board_frame, lv_color_hex(0x0B0F19), 0);
    lv_obj_set_style_border_color(board_frame, lv_color_hex(0x1F293D), 0);
    lv_obj_set_style_border_width(board_frame, 2, 0);
    lv_obj_set_style_radius(board_frame, 6, 0);
    lv_obj_set_style_pad_all(board_frame, 0, 0);
    lv_obj_remove_flag(board_frame, LV_OBJ_FLAG_SCROLLABLE);

    LV_DRAW_BUF_INIT_STATIC(s_board_buf);
    s_board_canvas = lv_canvas_create(s_scr);
    lv_canvas_set_draw_buf(s_board_canvas, &s_board_buf);
    for (uint8_t i = 0; i < PALETTE_SIZE; i++) {
        lv_canvas_set_palette(s_board_canvas, i,
                              lv_color_to_32(lv_color_hex(PALETTE[i]), LV_OPA_COVER));
    }
    lv_obj_set_pos(s_board_canvas, 20, 44);

    // ==========================================
    // Bottom Controls Bar (y: 290)
    // ==========================================
    lv_obj_t *ctl_bar = lv_label_create(s_scr);
    lv_obj_set_pos(ctl_bar, 5, 292);
    lv_obj_set_size(ctl_bar, 230, 24);
    lv_obj_set_style_text_align(ctl_bar, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ctl_bar, &lv_font_cn_16, 0);
    lv_obj_set_style_text_color(ctl_bar, lv_color_hex(0x566882), 0);
    lv_label_set_text(ctl_bar, "上: 左转  下: 右转  确认: 暂停");

    // ==========================================
    // Central Modal Overlay (Title / Pause / Game Over)
    // ==========================================
    s_modal = lv_obj_create(s_scr);
    lv_obj_set_pos(s_modal, 20, 72);
    lv_obj_set_size(s_modal, 200, 180);
    lv_obj_set_style_bg_color(s_modal, lv_color_hex(0x0A101E), 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_90, 0);
    lv_obj_set_style_border_color(s_modal, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(s_modal, 2, 0);
    lv_obj_set_style_radius(s_modal, 8, 0);
    lv_obj_set_style_pad_all(s_modal, 6, 0);
    lv_obj_remove_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    s_modal_title = lv_label_create(s_modal);
    lv_obj_set_pos(s_modal_title, 0, 12);
    lv_obj_set_size(s_modal_title, 188, 26);
    lv_obj_set_style_text_align(s_modal_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_modal_title, &lv_font_cn_16, 0);
    lv_obj_set_style_text_color(s_modal_title, lv_color_hex(0x00E676), 0);

    s_modal_sub = lv_label_create(s_modal);
    lv_obj_set_pos(s_modal_sub, 0, 48);
    lv_obj_set_size(s_modal_sub, 188, 24);
    lv_obj_set_style_text_align(s_modal_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_modal_sub, &lv_font_cn_16, 0);
    lv_obj_set_style_text_color(s_modal_sub, lv_color_hex(0x7F92AA), 0);

    s_modal_extra = lv_label_create(s_modal);
    lv_obj_set_pos(s_modal_extra, 0, 84);
    lv_obj_set_size(s_modal_extra, 188, 24);
    lv_obj_set_style_text_align(s_modal_extra, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_modal_extra, &lv_font_cn_16, 0);
    lv_obj_set_style_text_color(s_modal_extra, lv_color_hex(0xFFD600), 0);

    s_modal_prompt = lv_label_create(s_modal);
    lv_obj_set_pos(s_modal_prompt, 0, 130);
    lv_obj_set_size(s_modal_prompt, 188, 26);
    lv_obj_set_style_text_align(s_modal_prompt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_modal_prompt, &lv_font_cn_16, 0);
    lv_obj_set_style_text_color(s_modal_prompt, lv_color_hex(0xFFFFFF), 0);

    lv_screen_load(s_scr);
}

void snake_ui_update(const snake_game_t *game, int battery_soc) {
    if (!s_scr) return;

    // 更新分数
    lv_label_set_text_fmt(s_score_val, "%05u", (unsigned)game->score);
    lv_label_set_text_fmt(s_hi_val, "%05u", (unsigned)game->high_score);

    // 更新电量
    if (battery_soc >= 0) {
        lv_label_set_text_fmt(s_battery_val, "电量 %d%%", battery_soc);
        if (battery_soc <= 15) {
            lv_obj_set_style_text_color(s_battery_val, lv_color_hex(0xFF1744), 0);
        } else {
            lv_obj_set_style_text_color(s_battery_val, lv_color_hex(0x00E676), 0);
        }
    } else {
        lv_label_set_text(s_battery_val, "电量 --");
        lv_obj_set_style_text_color(s_battery_val, lv_color_hex(0x7F92AA), 0);
    }

    // 渲染游戏画布
    render_board(game);

    // 更新弹窗状态
    switch (game->state) {
    case SNAKE_STATE_TITLE:
        lv_obj_remove_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(s_modal, lv_color_hex(0x00E676), 0);
        lv_label_set_text(s_modal_title, "贪 吃 蛇");
        lv_obj_set_style_text_color(s_modal_title, lv_color_hex(0x00E676), 0);
        lv_label_set_text(s_modal_sub, "经典复古街机");
        lv_label_set_text_fmt(s_modal_extra, "最高纪录: %05u", (unsigned)game->high_score);
        lv_label_set_text(s_modal_prompt, "按 [确认] 开始游戏");
        break;

    case SNAKE_STATE_PLAYING:
        lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
        break;

    case SNAKE_STATE_PAUSED:
        lv_obj_remove_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(s_modal, lv_color_hex(0x00E5FF), 0);
        lv_label_set_text(s_modal_title, "游戏暂停");
        lv_obj_set_style_text_color(s_modal_title, lv_color_hex(0x00E5FF), 0);
        lv_label_set_text(s_modal_sub, "休息一下");
        lv_label_set_text(s_modal_extra, "");
        lv_label_set_text(s_modal_prompt, "按 [确认] 继续游戏");
        break;

    case SNAKE_STATE_GAME_OVER:
        lv_obj_remove_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(s_modal, lv_color_hex(0xFF1744), 0);
        lv_label_set_text(s_modal_title, "游戏结束");
        lv_obj_set_style_text_color(s_modal_title, lv_color_hex(0xFF1744), 0);
        lv_label_set_text_fmt(s_modal_sub, "本局得分: %u", (unsigned)game->score);
        if (game->is_new_high_score) {
            lv_label_set_text(s_modal_extra, "★ 刷新最高纪录! ★");
        } else {
            lv_label_set_text_fmt(s_modal_extra, "历史最高: %u", (unsigned)game->high_score);
        }
        lv_label_set_text(s_modal_prompt, "按 [确认] 再来一局");
        break;
    }
}

void snake_ui_destroy(void) {
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_board_canvas = NULL;
    }
}
