// main/tianji_ui.c — 天机·六爻: 极简现代国风 UI 界面实现
#include "tianji_ui.h"
#include "tianji_coins.h"
#include "tianji_audio.h"
#include "tianji_logic.h"
#include "tianji_net.h"
#include "bsp_display.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_random.h"
static uint32_t get_hardware_entropy(void) {
    return esp_random();
}
#else
static uint32_t get_hardware_entropy(void) {
    return (uint32_t)rand();
}
#endif

static const char *TAG = "tianji_ui";

// 色彩常量定义
#define COLOR_BG            0x0A0E17  // 深邃纯黑基底
#define COLOR_PANEL         0x141B2D  // 面板深蓝灰
#define COLOR_BORDER        0x232F48  // 线框灰
#define COLOR_CYAN          0x00E5FF  // 科技青 (本卦、静阳)
#define COLOR_AMBER         0xFFB300  // 琥珀金 (变卦、变爻、动爻)
#define COLOR_WHITE         0xF8FAFC  // 纯净白
#define COLOR_GRAY          0x8492A6  // 次级说明灰
#define COLOR_MUTED         0x334155  // 未掷暗线条底色

static tianji_session_t s_session;
static tianji_view_t s_current_view = TIANJI_VIEW_STANDBY;
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_ai_card = NULL;
static int s_battery_soc = -1;
static uint8_t s_reading_page = 0; // 0: 卦辞象曰, 1: 事业求财, 2: 姻缘变局

static void refresh_view(void);

// 顶部状态栏 (纯净中文，右上角电量)
static void build_header(lv_obj_t *parent, const char *title) {
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_size(header, 240, 26);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);

    // 标题 (设定固定最大宽度，杜绝与右侧状态徽标发生任何重合)
    lv_obj_t *lbl_title = lv_label_create(header);
    lv_obj_set_style_text_font(lbl_title, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(COLOR_CYAN), 0);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_width(lbl_title, 130);
    lv_obj_set_pos(lbl_title, 12, 4);

    // 状态小徽标 (置于电量左侧)
    lv_obj_t *lbl_net = lv_label_create(header);
    lv_obj_set_style_text_font(lbl_net, &tianji_font_16, 0);
    if (tianji_net_is_enabled()) {
        tianji_net_state_t st = tianji_net_get_state();
        if (st == TIANJI_NET_STATE_CONNECTED || st == TIANJI_NET_STATE_SUCCESS) {
            lv_obj_set_style_text_color(lbl_net, lv_color_hex(COLOR_CYAN), 0);
            lv_label_set_text(lbl_net, "联机");
        } else if (st == TIANJI_NET_STATE_INFERENCING) {
            lv_obj_set_style_text_color(lbl_net, lv_color_hex(COLOR_AMBER), 0);
            lv_label_set_text(lbl_net, "推演");
        } else if (st == TIANJI_NET_STATE_INFER_ERROR) {
            lv_obj_set_style_text_color(lbl_net, lv_color_hex(COLOR_AMBER), 0);
            lv_label_set_text(lbl_net, "受阻");
        } else {
            lv_obj_set_style_text_color(lbl_net, lv_color_hex(COLOR_GRAY), 0);
            lv_label_set_text(lbl_net, "连接");
        }
    } else {
        lv_obj_set_style_text_color(lbl_net, lv_color_hex(COLOR_MUTED), 0);
        lv_label_set_text(lbl_net, "脱机");
    }
    lv_obj_align(lbl_net, LV_ALIGN_TOP_RIGHT, -54, 4);

    // 右上角电量指示
    lv_obj_t *lbl_bat = lv_label_create(header);
    lv_obj_set_style_text_font(lbl_bat, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_bat, lv_color_hex(COLOR_GRAY), 0);
    if (s_battery_soc >= 0) {
        lv_label_set_text_fmt(lbl_bat, "%d%%", s_battery_soc);
    } else {
        lv_label_set_text(lbl_bat, "--%");
    }
    lv_obj_align(lbl_bat, LV_ALIGN_TOP_RIGHT, -12, 4);

    // 分割线
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_size(line, 240, 1);
    lv_obj_set_pos(line, 0, 26);
    lv_obj_set_style_bg_color(line, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
}

// 底部操作指示栏 (去除方括号与斜杠，优雅中文)
static void build_footer(lv_obj_t *parent, const char *prompt) {
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_size(line, 240, 1);
    lv_obj_set_pos(line, 0, 292);
    lv_obj_set_style_bg_color(line, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);

    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_WHITE), 0);
    lv_label_set_text(lbl, prompt);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -6);
}

// 绘制单根爻画 (x, y, 宽度, 是否阳爻, 颜色)
static void draw_yao_bar(lv_obj_t *parent, int x, int y, int total_w, int h, bool is_yang, uint32_t color_hex) {
    if (is_yang) {
        // 实线 (单长条)
        lv_obj_t *bar = lv_obj_create(parent);
        lv_obj_set_size(bar, total_w, h);
        lv_obj_set_pos(bar, x, y);
        lv_obj_set_style_bg_color(bar, lv_color_hex(color_hex), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 1, 0);
    } else {
        // 断线 (两段中间空缺)
        int seg_w = (total_w - 10) / 2;
        lv_obj_t *bar1 = lv_obj_create(parent);
        lv_obj_set_size(bar1, seg_w, h);
        lv_obj_set_pos(bar1, x, y);
        lv_obj_set_style_bg_color(bar1, lv_color_hex(color_hex), 0);
        lv_obj_set_style_bg_opa(bar1, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar1, 0, 0);
        lv_obj_set_style_radius(bar1, 1, 0);

        lv_obj_t *bar2 = lv_obj_create(parent);
        lv_obj_set_size(bar2, seg_w, h);
        lv_obj_set_pos(bar2, x + seg_w + 10, y);
        lv_obj_set_style_bg_color(bar2, lv_color_hex(color_hex), 0);
        lv_obj_set_style_bg_opa(bar2, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar2, 0, 0);
        lv_obj_set_style_radius(bar2, 1, 0);
    }
}

// ============================================================================
// 界面 1: 待命 / 准备起卦页
// ============================================================================
static void render_view_standby(void) {
    build_header(s_screen, "天机 · 六爻");

    lv_obj_t *card = lv_obj_create(s_screen);
    lv_obj_set_size(card, 216, 236);
    lv_obj_set_pos(card, 12, 38);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    // 主标（居中，纯净中文，不带拼音）
    lv_obj_t *title = lv_label_create(card);
    lv_obj_set_style_text_font(title, &tianji_font_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_CYAN), 0);
    lv_label_set_text(title, "天机 · 六爻");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    // 心法提示（居中）
    lv_obj_t *tip = lv_label_create(card);
    lv_obj_set_style_text_font(tip, &tianji_font_16, 0);
    lv_obj_set_style_text_color(tip, lv_color_hex(COLOR_WHITE), 0);
    lv_label_set_text(tip, "凝神静虑，意在所求");
    lv_obj_align(tip, LV_ALIGN_TOP_MID, 0, 44);

    // 真实铜钱正反面图像展示箱（居中）
    lv_obj_t *coins_box = lv_obj_create(card);
    lv_obj_set_size(coins_box, 192, 60);
    lv_obj_align(coins_box, LV_ALIGN_TOP_MID, 0, 74);
    lv_obj_set_style_bg_color(coins_box, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_color(coins_box, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(coins_box, 1, 0);
    lv_obj_set_style_radius(coins_box, 4, 0);
    lv_obj_set_style_pad_all(coins_box, 0, 0);
    lv_obj_set_scrollbar_mode(coins_box, LV_SCROLLBAR_MODE_OFF);

    // 展示三枚铜钱真实图片（正面、背面、正面对称居中排列）
    lv_obj_t *c1 = lv_image_create(coins_box);
    lv_image_set_src(c1, &tianji_coin_front);
    lv_obj_set_pos(c1, 22, 13);

    lv_obj_t *c2 = lv_image_create(coins_box);
    lv_image_set_src(c2, &tianji_coin_back);
    lv_obj_set_pos(c2, 79, 13);

    lv_obj_t *c3 = lv_image_create(coins_box);
    lv_image_set_src(c3, &tianji_coin_front);
    lv_obj_set_pos(c3, 136, 13);

    // 装饰细线（居中分隔）
    lv_obj_t *dline = lv_obj_create(card);
    lv_obj_set_size(dline, 160, 1);
    lv_obj_align(dline, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_set_style_bg_color(dline, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(dline, 0, 0);

    // 运行状态与模式说明（全居中对齐，字距行距匀称，消除偏左视觉）
    lv_obj_t *mode_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(mode_lbl, &tianji_font_16, 0);
    lv_obj_set_style_text_color(mode_lbl, lv_color_hex(COLOR_GRAY), 0);
    lv_obj_set_style_text_align(mode_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(mode_lbl, 6, 0);
    if (tianji_net_is_enabled()) {
        lv_label_set_text(mode_lbl, "起卦法则：三钱古法\n运行模式：联机模式");
    } else {
        lv_label_set_text(mode_lbl, "起卦法则：三钱古法\n运行模式：脱机模式");
    }
    lv_obj_align(mode_lbl, LV_ALIGN_TOP_MID, 0, 166);

    build_footer(s_screen, "短按确定起卦  长按2秒切换模式");
}

// ============================================================================
// 界面 2: 逐次起卦推演页 (6次逐次抛掷)
// ============================================================================
static void render_view_casting(void) {
    char header_buf[64];
    snprintf(header_buf, sizeof(header_buf), "起卦推演 第%d爻 共6爻", s_session.current_line);
    build_header(s_screen, header_buf);

    // 顶部当前投掷铜钱图像与性质展示卡片
    lv_obj_t *coin_bar = lv_obj_create(s_screen);
    lv_obj_set_size(coin_bar, 216, 44);
    lv_obj_set_pos(coin_bar, 12, 34);
    lv_obj_set_style_bg_color(coin_bar, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_color(coin_bar, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(coin_bar, 1, 0);
    lv_obj_set_style_pad_all(coin_bar, 0, 0);
    lv_obj_set_scrollbar_mode(coin_bar, LV_SCROLLBAR_MODE_OFF);

    if (s_session.current_line > 0) {
        int last_idx = s_session.current_line - 1;
        const tianji_toss_result_t *t = &s_session.toss_history[last_idx];
        const char *tname = "少阳";
        if (t->type == TIANJI_LINE_LAOYIN) tname = "老阴（变阳）";
        else if (t->type == TIANJI_LINE_SHAOYIN) tname = "少阴";
        else if (t->type == TIANJI_LINE_LAOYANG) tname = "老阳（变阴）";

        // 三枚硬币采用真实图片显示正反面（对称排列）
        lv_obj_t *img0 = lv_image_create(coin_bar);
        lv_image_set_src(img0, t->coins[0] ? &tianji_coin_front : &tianji_coin_back);
        lv_obj_set_pos(img0, 10, 5);

        lv_obj_t *img1 = lv_image_create(coin_bar);
        lv_image_set_src(img1, t->coins[1] ? &tianji_coin_front : &tianji_coin_back);
        lv_obj_set_pos(img1, 48, 5);

        lv_obj_t *img2 = lv_image_create(coin_bar);
        lv_image_set_src(img2, t->coins[2] ? &tianji_coin_front : &tianji_coin_back);
        lv_obj_set_pos(img2, 86, 5);

        // 爻名结论
        lv_obj_t *coin_lbl = lv_label_create(coin_bar);
        lv_obj_set_style_text_font(coin_lbl, &tianji_font_16, 0);
        lv_obj_set_style_text_color(coin_lbl, tianji_line_is_moving(t->type) ? lv_color_hex(COLOR_AMBER) : lv_color_hex(COLOR_CYAN), 0);
        lv_label_set_text(coin_lbl, tname);
        lv_obj_set_pos(coin_lbl, 126, 13);
    } else {
        lv_obj_t *coin_lbl = lv_label_create(coin_bar);
        lv_obj_set_style_text_font(coin_lbl, &tianji_font_16, 0);
        lv_label_set_text(coin_lbl, "初爻待掷，请按确定键");
        lv_obj_set_style_text_color(coin_lbl, lv_color_hex(COLOR_GRAY), 0);
        lv_obj_center(coin_lbl);
    }

    // 六爻绘制主面板
    lv_obj_t *yao_panel = lv_obj_create(s_screen);
    lv_obj_set_size(yao_panel, 216, 198);
    lv_obj_set_pos(yao_panel, 12, 82);
    lv_obj_set_style_bg_color(yao_panel, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_color(yao_panel, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(yao_panel, 1, 0);
    lv_obj_set_style_pad_all(yao_panel, 0, 0);
    lv_obj_set_scrollbar_mode(yao_panel, LV_SCROLLBAR_MODE_OFF);

    const char *line_names[6] = {"初爻", "二爻", "三爻", "四爻", "五爻", "上爻"};
    int start_y = 12;
    int line_gap = 29;

    for (int i = 5; i >= 0; i--) {
        int cur_y = start_y + (5 - i) * line_gap;

        // 爻位标签 (如 "初爻", "二爻"，对称间距)
        lv_obj_t *lbl = lv_label_create(yao_panel);
        lv_obj_set_style_text_font(lbl, &tianji_font_16, 0);
        lv_obj_set_pos(lbl, 14, cur_y - 2);

        if (i < s_session.current_line) {
            tianji_line_type_t ltype = s_session.lines[i];
            bool yang = tianji_line_is_yang(ltype);
            bool moving = tianji_line_is_moving(ltype);
            uint32_t col = moving ? COLOR_AMBER : COLOR_CYAN;

            lv_label_set_text(lbl, line_names[i]);
            lv_obj_set_style_text_color(lbl, lv_color_hex(col), 0);

            // 绘制爻线 (宽 98px, 高 8px)
            draw_yao_bar(yao_panel, 54, cur_y + 2, 98, 8, yang, col);

            // 动爻标注 (清晰标注 阳动 / 阴动)
            if (moving) {
                lv_obj_t *m_lbl = lv_label_create(yao_panel);
                lv_obj_set_style_text_font(m_lbl, &tianji_font_16, 0);
                lv_obj_set_style_text_color(m_lbl, lv_color_hex(COLOR_AMBER), 0);
                lv_label_set_text(m_lbl, (ltype == TIANJI_LINE_LAOYANG) ? "阳动" : "阴动");
                lv_obj_set_pos(m_lbl, 164, cur_y - 2);
            }
        } else {
            // 待生成的爻
            lv_label_set_text(lbl, line_names[i]);
            lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_MUTED), 0);

            lv_obj_t *dim = lv_obj_create(yao_panel);
            lv_obj_set_size(dim, 98, 2);
            lv_obj_set_pos(dim, 54, cur_y + 5);
            lv_obj_set_style_bg_color(dim, lv_color_hex(COLOR_MUTED), 0);
            lv_obj_set_style_border_width(dim, 0, 0);

            if (i == s_session.current_line) {
                // 当前正等待掷的一爻 (对称居中)
                lv_obj_t *target = lv_label_create(yao_panel);
                lv_obj_set_style_text_font(target, &tianji_font_16, 0);
                lv_obj_set_style_text_color(target, lv_color_hex(COLOR_WHITE), 0);
                lv_label_set_text(target, "待掷");
                lv_obj_set_pos(target, 164, cur_y - 2);
            }
        }
    }

    char footer_buf[64];
    if (s_session.current_line < 6) {
        snprintf(footer_buf, sizeof(footer_buf), "按确定键掷%s", line_names[s_session.current_line]);
    } else {
        snprintf(footer_buf, sizeof(footer_buf), "六爻已成，按确定键排盘");
    }
    build_footer(s_screen, footer_buf);
}

// ============================================================================
// 界面 3: 双盘全息对比总览页 (本卦 vs 变卦，两栏严格对称)
// ============================================================================
static void render_view_overview(void) {
    const tianji_hexagram_t *prim = tianji_get_hexagram(s_session.primary_bits);
    const tianji_hexagram_t *trans = tianji_get_hexagram(s_session.transformed_bits);

    char hbuf[64];
    snprintf(hbuf, sizeof(hbuf), "排盘结果 动爻%d位", s_session.moving_count);
    build_header(s_screen, hbuf);

    lv_obj_t *card = lv_obj_create(s_screen);
    lv_obj_set_size(card, 216, 246);
    lv_obj_set_pos(card, 12, 36);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    // ==========================================
    // 左栏: 本卦 (x = 16..96, 独立成列，左边距16px)
    // ==========================================
    lv_obj_t *lbl_prim_badge = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_prim_badge, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_prim_badge, lv_color_hex(COLOR_CYAN), 0);
    lv_label_set_text(lbl_prim_badge, "本卦");
    lv_obj_set_pos(lbl_prim_badge, 16, 6);

    lv_obj_t *lbl_prim_title = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_prim_title, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_prim_title, lv_color_hex(COLOR_WHITE), 0);
    lv_label_set_text(lbl_prim_title, prim->name); // 纯净卦名，不带拼音
    lv_obj_set_pos(lbl_prim_title, 16, 26);

    lv_obj_t *lbl_prim_sub = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_prim_sub, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_prim_sub, lv_color_hex(COLOR_GRAY), 0);
    lv_label_set_text_fmt(lbl_prim_sub, "%s上%s下", prim->upper_name, prim->lower_name);
    lv_obj_set_pos(lbl_prim_sub, 16, 46);

    // ==========================================
    // 右栏: 变卦 (x = 120..200, 独立成列，右边距16px完全对称)
    // ==========================================
    lv_obj_t *lbl_trans_badge = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_trans_badge, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_trans_badge, lv_color_hex(COLOR_AMBER), 0);
    lv_label_set_text(lbl_trans_badge, "变卦");
    lv_obj_set_pos(lbl_trans_badge, 120, 6);

    lv_obj_t *lbl_trans_title = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_trans_title, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_trans_title, lv_color_hex(COLOR_WHITE), 0);
    lv_label_set_text(lbl_trans_title, trans->name); // 纯净卦名，不带拼音
    lv_obj_set_pos(lbl_trans_title, 120, 26);

    lv_obj_t *lbl_trans_sub = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_trans_sub, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_trans_sub, lv_color_hex(COLOR_GRAY), 0);
    lv_label_set_text_fmt(lbl_trans_sub, "%s上%s下", trans->upper_name, trans->lower_name);
    lv_obj_set_pos(lbl_trans_sub, 120, 46);

    // 并列绘制六爻 (左栏 16..96, 右栏 120..200, 宽度均为 80px, 边距严格对称)
    int y_start = 66;
    int gap = 16;
    for (int i = 5; i >= 0; i--) {
        int cur_y = y_start + (5 - i) * gap;
        bool p_yang = tianji_line_is_yang(s_session.lines[i]);
        bool moving = tianji_line_is_moving(s_session.lines[i]);
        bool t_yang = tianji_line_transformed_is_yang(s_session.lines[i]);

        draw_yao_bar(card, 16, cur_y, 80, 6, p_yang, moving ? COLOR_AMBER : COLOR_CYAN);
        draw_yao_bar(card, 120, cur_y, 80, 6, t_yang, moving ? COLOR_AMBER : COLOR_GRAY);
    }

    // 分割线
    lv_obj_t *sline = lv_obj_create(card);
    lv_obj_set_size(sline, 216, 1);
    lv_obj_set_pos(sline, 0, 168);
    lv_obj_set_style_bg_color(sline, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(sline, 0, 0);

    // 变局解断摘要 (两边边距各12px，对称居中)
    lv_obj_t *lbl_summary = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_summary, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_summary, lv_color_hex(COLOR_WHITE), 0);
    lv_obj_set_width(lbl_summary, 192);
    lv_label_set_long_mode(lbl_summary, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(lbl_summary, 12, 174);

    if (s_session.moving_count == 0) {
        lv_label_set_text_fmt(lbl_summary, "六爻皆静：以本卦卦辞为主\n%s", prim->judgment);
    } else {
        lv_label_set_text_fmt(lbl_summary, "%s之%s，%d位动爻变化\n走向：%s",
            prim->short_name, trans->short_name, s_session.moving_count, trans->xiang);
    }

    if (tianji_net_is_enabled()) {
        build_footer(s_screen, "短按OK感召玄机  下键看卦象");
    } else {
        build_footer(s_screen, "下键看断语，长按2秒重起");
    }
}

// 异步 AI 推理状态更新回调
static void on_ai_status_update(tianji_net_state_t state, const char *msg) {
    (void)state;
    (void)msg;
    if (bsp_lvgl_lock(100)) {
        if (s_current_view == TIANJI_VIEW_AI) {
            refresh_view();
        }
        bsp_lvgl_unlock();
    }
}

// ============================================================================
// 界面 5: 天机神思 / 深度推演解卦页
// ============================================================================
static void render_view_ai(void) {
    build_header(s_screen, "天机神思");

    lv_obj_t *card = lv_obj_create(s_screen);
    s_ai_card = card;
    lv_obj_set_size(card, 216, 246);
    lv_obj_set_pos(card, 12, 36);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *content = lv_label_create(card);
    lv_obj_set_style_text_font(content, &tianji_font_16, 0);
    lv_obj_set_width(content, 196);
    lv_label_set_long_mode(content, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(content, 0, 0);

    tianji_net_state_t state = tianji_net_get_state();
    if (state == TIANJI_NET_STATE_INFERENCING) {
        lv_obj_set_style_text_color(content, lv_color_hex(COLOR_CYAN), 0);
        lv_label_set_text(content,
            "【神思冥合中】\n\n"
            "一念通天地，六爻动乾坤。\n\n"
            "玄机如丝，正在感应远方宿主天地至理……\n\n"
            "请凝神静虑，静候天机批示。");
    } else if (state == TIANJI_NET_STATE_INFER_ERROR) {
        lv_obj_set_style_text_color(content, lv_color_hex(COLOR_AMBER), 0);
        lv_label_set_text_fmt(content, "【天机未合】\n\n%s\n\n请按确定键返回", tianji_net_get_ai_result());
    } else {
        lv_obj_set_style_text_color(content, lv_color_hex(COLOR_WHITE), 0);
        const char *res = tianji_net_get_ai_result();
        if (res && strlen(res) > 0) {
            lv_label_set_text_fmt(content, "【天机神断】\n\n%s", res);
        } else {
            lv_label_set_text(content, "【天机隐匿】\n\n暂未感应到断语，请按确定键返回重试。");
        }
    }

    build_footer(s_screen, "上下键滚动，确定键返回");
}

// ============================================================================
// 界面 4: 离线断语与经文详情页 (在详情页保留生僻字拼音标注)
// ============================================================================
static void render_view_reading(void) {
    const tianji_hexagram_t *prim = tianji_get_hexagram(s_session.primary_bits);

    char hbuf[64];
    snprintf(hbuf, sizeof(hbuf), "%s 第%d页 共3页", prim->short_name, s_reading_page + 1);
    build_header(s_screen, hbuf);

    lv_obj_t *card = lv_obj_create(s_screen);
    lv_obj_set_size(card, 216, 246);
    lv_obj_set_pos(card, 12, 36);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *content = lv_label_create(card);
    lv_obj_set_style_text_font(content, &tianji_font_16, 0);
    lv_obj_set_style_text_color(content, lv_color_hex(COLOR_WHITE), 0);
    lv_obj_set_width(content, 200);
    lv_label_set_long_mode(content, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(content, 8, 8);

    if (s_reading_page == 0) {
        // 第一页: 卦名拼音、卦辞与象曰
        lv_label_set_text_fmt(content,
            "【卦名】%s\n【卦辞】\n%s\n\n【象曰】\n%s",
            prim->pinyin_name, prim->judgment, prim->xiang);
    } else if (s_reading_page == 1) {
        // 第二页: 事业与求财
        lv_label_set_text_fmt(content,
            "【事业功名】\n%s\n\n【求财进益】\n%s",
            prim->career, prim->wealth);
    } else {
        // 第三页: 姻缘与动爻走向
        const tianji_hexagram_t *trans = tianji_get_hexagram(s_session.transformed_bits);
        lv_label_set_text_fmt(content,
            "【姻缘情感】\n%s\n\n【变局启示】\n变见【%s】：\n%s",
            prim->relationship, trans->pinyin_name, trans->judgment);
    }

    build_footer(s_screen, "上下键翻页，确定键返回");
}

static void refresh_view(void) {
    if (!s_screen) return;
    lv_obj_clean(s_screen);

    switch (s_current_view) {
    case TIANJI_VIEW_STANDBY:
        render_view_standby();
        break;
    case TIANJI_VIEW_CASTING:
        render_view_casting();
        break;
    case TIANJI_VIEW_OVERVIEW:
        render_view_overview();
        break;
    case TIANJI_VIEW_READING:
        render_view_reading();
        break;
    case TIANJI_VIEW_AI:
        render_view_ai();
        break;
    default:
        render_view_standby();
        break;
    }
}

void tianji_ui_init(void) {
    tianji_session_init(&s_session);
    s_current_view = TIANJI_VIEW_STANDBY;
    s_reading_page = 0;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_OFF);

    refresh_view();
    lv_screen_load(s_screen);
    ESP_LOGI(TAG, "Tianji Liu Yao UI initialized");
}

void tianji_ui_destroy(void) {
    if (s_screen) {
        lv_obj_clean(s_screen);
        lv_obj_delete(s_screen);
        s_screen = NULL;
        s_ai_card = NULL;
    }
}

void tianji_ui_update_battery(int soc) {
    s_battery_soc = soc;
    refresh_view();
}

tianji_view_t tianji_ui_get_current_view(void) {
    return s_current_view;
}

void tianji_ui_handle_key(tianji_key_action_t action) {
    switch (s_current_view) {
    case TIANJI_VIEW_STANDBY:
        if (action == TIANJI_KEY_OK_CLICK) {
            tianji_session_init(&s_session);
            s_current_view = TIANJI_VIEW_CASTING;
            tianji_audio_play(TIANJI_SFX_START);
            refresh_view();
        } else if (action == TIANJI_KEY_OK_LONG) {
            // 首页长按 OK 键（2秒）：切换联机与离线模式
            bool new_mode = !tianji_net_is_enabled();
            tianji_net_set_enabled(new_mode);
            tianji_audio_play(TIANJI_SFX_HEX_COMPLETE);
            refresh_view();
        }
        break;

    case TIANJI_VIEW_CASTING:
        if (action == TIANJI_KEY_OK_CLICK) {
            if (s_session.current_line < 6) {
                tianji_toss_result_t toss;
                // 使用 ESP32-C3 硬件随机数发生器起卦
                tianji_session_cast_step(&s_session, get_hardware_entropy, &toss);
                tianji_audio_play(TIANJI_SFX_TOSS);
                refresh_view();

                if (s_session.current_line >= 6) {
                    tianji_audio_play(TIANJI_SFX_HEX_COMPLETE);
                }
            } else {
                s_current_view = TIANJI_VIEW_OVERVIEW;
                tianji_audio_play(TIANJI_SFX_LINE_REVEAL);
                refresh_view();
            }
        } else if (action == TIANJI_KEY_OK_LONG) {
            // 长按 2 秒重置
            s_current_view = TIANJI_VIEW_STANDBY;
            tianji_audio_play(TIANJI_SFX_PAGE_FLIP);
            refresh_view();
        }
        break;

    case TIANJI_VIEW_OVERVIEW:
        if (action == TIANJI_KEY_OK_CLICK && tianji_net_is_enabled()) {
            // 联机模式下短按 OK 呼叫 oMLX (MiniCPM 5)
            s_current_view = TIANJI_VIEW_AI;
            tianji_audio_play(TIANJI_SFX_LINE_REVEAL);
            refresh_view();
            tianji_net_request_ai(&s_session, on_ai_status_update);
        } else if (action == TIANJI_KEY_DOWN_CLICK || action == TIANJI_KEY_OK_CLICK) {
            s_current_view = TIANJI_VIEW_READING;
            s_reading_page = 0;
            tianji_audio_play(TIANJI_SFX_PAGE_FLIP);
            refresh_view();
        } else if (action == TIANJI_KEY_OK_LONG) {
            // 长按 2 秒重起
            s_current_view = TIANJI_VIEW_STANDBY;
            tianji_audio_play(TIANJI_SFX_START);
            refresh_view();
        }
        break;

    case TIANJI_VIEW_READING:
        if (action == TIANJI_KEY_DOWN_CLICK) {
            if (s_reading_page < 2) {
                s_reading_page++;
                tianji_audio_play(TIANJI_SFX_PAGE_FLIP);
                refresh_view();
            }
        } else if (action == TIANJI_KEY_UP_CLICK) {
            if (s_reading_page > 0) {
                s_reading_page--;
                tianji_audio_play(TIANJI_SFX_PAGE_FLIP);
                refresh_view();
            }
        } else if (action == TIANJI_KEY_OK_CLICK) {
            s_current_view = TIANJI_VIEW_OVERVIEW;
            tianji_audio_play(TIANJI_SFX_PAGE_FLIP);
            refresh_view();
        } else if (action == TIANJI_KEY_OK_LONG) {
            // 长按 2 秒重起
            s_current_view = TIANJI_VIEW_STANDBY;
            tianji_audio_play(TIANJI_SFX_START);
            refresh_view();
        }
        break;

    case TIANJI_VIEW_AI:
        if (action == TIANJI_KEY_DOWN_CLICK) {
            if (s_ai_card) lv_obj_scroll_by_bounded(s_ai_card, 0, -40, LV_ANIM_ON);
        } else if (action == TIANJI_KEY_UP_CLICK) {
            if (s_ai_card) lv_obj_scroll_by_bounded(s_ai_card, 0, 40, LV_ANIM_ON);
        } else if (action == TIANJI_KEY_OK_CLICK) {
            s_current_view = TIANJI_VIEW_OVERVIEW;
            tianji_audio_play(TIANJI_SFX_PAGE_FLIP);
            refresh_view();
        } else if (action == TIANJI_KEY_OK_LONG) {
            s_current_view = TIANJI_VIEW_STANDBY;
            tianji_audio_play(TIANJI_SFX_START);
            refresh_view();
        }
        break;

    default:
        break;
    }
}
