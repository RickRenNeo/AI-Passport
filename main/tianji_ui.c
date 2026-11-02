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
static void on_voice_status_update(tianji_voice_state_t state, const char *msg);
static void on_ai_status_update(tianji_net_state_t state, const char *msg);

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

    // 标题
    lv_obj_t *lbl_title = lv_label_create(header);
    lv_obj_set_style_text_font(lbl_title, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(COLOR_CYAN), 0);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_width(lbl_title, 130);
    lv_obj_set_pos(lbl_title, 12, 4);

    // 状态徽标 (置于电量左侧)
    lv_obj_t *lbl_net = lv_label_create(header);
    lv_obj_set_style_text_font(lbl_net, &tianji_font_16, 0);
    if (tianji_net_is_enabled()) {
        if (s_current_view == TIANJI_VIEW_VOICE_LISTENING) {
            if (tianji_voice_is_recording()) {
                lv_obj_set_style_text_color(lbl_net, lv_color_hex(COLOR_CYAN), 0);
                lv_label_set_text(lbl_net, "倾听");
            } else {
                lv_obj_set_style_text_color(lbl_net, lv_color_hex(COLOR_AMBER), 0);
                lv_label_set_text(lbl_net, "审度");
            }
        } else if (s_current_view == TIANJI_VIEW_AI && tianji_voice_is_playing()) {
            lv_obj_set_style_text_color(lbl_net, lv_color_hex(COLOR_CYAN), 0);
            lv_label_set_text(lbl_net, "批断");
        } else {
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

// 底部操作指示栏
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

// 绘制单根爻画
static void draw_yao_bar(lv_obj_t *parent, int x, int y, int total_w, int h, bool is_yang, uint32_t color_hex) {
    if (is_yang) {
        lv_obj_t *bar = lv_obj_create(parent);
        lv_obj_set_size(bar, total_w, h);
        lv_obj_set_pos(bar, x, y);
        lv_obj_set_style_bg_color(bar, lv_color_hex(color_hex), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 1, 0);
    } else {
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
// 界面 1: 待命页
// ============================================================================
static void render_view_standby(void) {
    build_header(s_screen, "天机六爻");

    lv_obj_t *card = lv_obj_create(s_screen);
    lv_obj_set_size(card, 216, 246);
    lv_obj_set_pos(card, 12, 36);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *lbl_title = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_title, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(COLOR_CYAN), 0);
    lv_label_set_text(lbl_title, "【天机·六爻】随身掌机");
    lv_obj_set_pos(lbl_title, 0, 0);

    lv_obj_t *lbl_intro = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_intro, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_intro, lv_color_hex(COLOR_WHITE), 0);
    lv_obj_set_width(lbl_intro, 192);
    lv_label_set_long_mode(lbl_intro, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lbl_intro,
        "周易六十四卦周天推演。\n\n"
        "三钱诚掷，凝神动静。\n"
        "吉凶由心，机变在握。\n\n"
        "起卦前凝神默念欲测之事：\n"
        "求财、事业、姻缘、进退。");
    lv_obj_set_pos(lbl_intro, 0, 30);

    lv_obj_t *lbl_mode = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_mode, &tianji_font_16, 0);
    lv_obj_set_width(lbl_mode, 192);
    lv_label_set_long_mode(lbl_mode, LV_LABEL_LONG_WRAP);
    if (tianji_net_is_enabled()) {
        lv_obj_set_style_text_color(lbl_mode, lv_color_hex(COLOR_CYAN), 0);
        lv_label_set_text(lbl_mode, "【当前模式】语音网关联机模式\n支持天机语音直断");
    } else {
        lv_obj_set_style_text_color(lbl_mode, lv_color_hex(COLOR_GRAY), 0);
        lv_label_set_text(lbl_mode, "【当前模式】本地脱机经文模式\n长按OK键切换联机");
    }
    lv_obj_set_pos(lbl_mode, 0, 160);

    build_footer(s_screen, "按确定键凝神起卦");
}

// ============================================================================
// 界面 2: 逐次掷爻推演页
// ============================================================================
static void render_view_casting(void) {
    char hbuf[64];
    snprintf(hbuf, sizeof(hbuf), "掷爻推演 %d/6", s_session.current_line);
    build_header(s_screen, hbuf);

    lv_obj_t *card = lv_obj_create(s_screen);
    lv_obj_set_size(card, 216, 246);
    lv_obj_set_pos(card, 12, 36);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    int y_start = 12;
    int gap = 20;
    const char *names[6] = {"初爻", "二爻", "三爻", "四爻", "五爻", "上爻"};

    for (int i = 5; i >= 0; i--) {
        int cur_y = y_start + (5 - i) * gap;

        lv_obj_t *lbl_n = lv_label_create(card);
        lv_obj_set_style_text_font(lbl_n, &tianji_font_16, 0);
        lv_obj_set_style_text_color(lbl_n, lv_color_hex(COLOR_GRAY), 0);
        lv_label_set_text(lbl_n, names[i]);
        lv_obj_set_pos(lbl_n, 0, cur_y - 4);

        if (i < s_session.current_line) {
            bool is_yang = tianji_line_is_yang(s_session.lines[i]);
            bool is_mov = tianji_line_is_moving(s_session.lines[i]);
            uint32_t col = is_mov ? COLOR_AMBER : COLOR_CYAN;

            draw_yao_bar(card, 44, cur_y, 110, 8, is_yang, col);

            lv_obj_t *lbl_t = lv_label_create(card);
            lv_obj_set_style_text_font(lbl_t, &tianji_font_16, 0);
            lv_obj_set_style_text_color(lbl_t, lv_color_hex(col), 0);
            const char *desc = "";
            switch (s_session.lines[i]) {
            case TIANJI_LINE_OLD_YIN:    desc = "老阴○"; break;
            case TIANJI_LINE_YOUNG_YANG: desc = "少阳"; break;
            case TIANJI_LINE_YOUNG_YIN:  desc = "少阴"; break;
            case TIANJI_LINE_OLD_YANG:   desc = "老阳●"; break;
            }
            lv_label_set_text(lbl_t, desc);
            lv_obj_set_pos(lbl_t, 160, cur_y - 4);
        } else {
            draw_yao_bar(card, 44, cur_y, 110, 8, true, COLOR_MUTED);
        }
    }

    lv_obj_t *lbl_tip = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_tip, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_tip, lv_color_hex(COLOR_WHITE), 0);
    lv_obj_set_width(lbl_tip, 192);
    lv_label_set_long_mode(lbl_tip, LV_LABEL_LONG_WRAP);

    if (s_session.current_line < 6) {
        lv_label_set_text_fmt(lbl_tip, "正在掷第%s……\n请凝神默想心中所求之事", names[s_session.current_line]);
        build_footer(s_screen, "按确定键掷钱推爻");
    } else {
        lv_label_set_text(lbl_tip, "六爻已全，卦象大定！\n按确定键定盘揭示卦象。");
        build_footer(s_screen, "按确定键查看全息总览");
    }
    lv_obj_set_pos(lbl_tip, 0, 168);
}

// ============================================================================
// 界面 3: 双盘全息对比总览页
// ============================================================================
static void render_view_overview(void) {
    const tianji_hexagram_t *prim = tianji_get_hexagram(s_session.primary_bits);
    const tianji_hexagram_t *trans = tianji_get_hexagram(s_session.transformed_bits);

    build_header(s_screen, "卦象全息总览");

    lv_obj_t *card = lv_obj_create(s_screen);
    lv_obj_set_size(card, 216, 246);
    lv_obj_set_pos(card, 12, 36);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    // 左栏: 本卦
    lv_obj_t *lbl_prim_badge = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_prim_badge, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_prim_badge, lv_color_hex(COLOR_CYAN), 0);
    lv_label_set_text(lbl_prim_badge, "本卦");
    lv_obj_set_pos(lbl_prim_badge, 16, 6);

    lv_obj_t *lbl_prim_title = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_prim_title, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_prim_title, lv_color_hex(COLOR_WHITE), 0);
    lv_label_set_text(lbl_prim_title, prim->name);
    lv_obj_set_pos(lbl_prim_title, 16, 26);

    lv_obj_t *lbl_prim_sub = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_prim_sub, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_prim_sub, lv_color_hex(COLOR_GRAY), 0);
    lv_label_set_text_fmt(lbl_prim_sub, "%s上%s下", prim->upper_name, prim->lower_name);
    lv_obj_set_pos(lbl_prim_sub, 16, 46);

    // 右栏: 变卦
    lv_obj_t *lbl_trans_badge = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_trans_badge, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_trans_badge, lv_color_hex(COLOR_AMBER), 0);
    lv_label_set_text(lbl_trans_badge, "变卦");
    lv_obj_set_pos(lbl_trans_badge, 120, 6);

    lv_obj_t *lbl_trans_title = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_trans_title, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_trans_title, lv_color_hex(COLOR_WHITE), 0);
    lv_label_set_text(lbl_trans_title, trans->name);
    lv_obj_set_pos(lbl_trans_title, 120, 26);

    lv_obj_t *lbl_trans_sub = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_trans_sub, &tianji_font_16, 0);
    lv_obj_set_style_text_color(lbl_trans_sub, lv_color_hex(COLOR_GRAY), 0);
    lv_label_set_text_fmt(lbl_trans_sub, "%s上%s下", trans->upper_name, trans->lower_name);
    lv_obj_set_pos(lbl_trans_sub, 120, 46);

    // 并列绘制六爻
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

    // 变局诊断摘要
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
        build_footer(s_screen, "确定键说出所测之事  下键看经文");
    } else {
        build_footer(s_screen, "下键看经文，长按2秒重起");
    }
}

// ============================================================================
// 界面 4: 离线断语与经文详情页
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
        lv_label_set_text_fmt(content,
            "【卦名】%s\n【卦辞】\n%s\n\n【象曰】\n%s",
            prim->pinyin_name, prim->judgment, prim->xiang);
    } else if (s_reading_page == 1) {
        lv_label_set_text_fmt(content,
            "【事业功名】\n%s\n\n【求财进益】\n%s",
            prim->career, prim->wealth);
    } else {
        const tianji_hexagram_t *trans = tianji_get_hexagram(s_session.transformed_bits);
        lv_label_set_text_fmt(content,
            "【姻缘情感】\n%s\n\n【变局启示】\n变见【%s】：\n%s",
            prim->relationship, trans->pinyin_name, trans->judgment);
    }

    build_footer(s_screen, "上下键翻页，确定键返回");
}

// ============================================================================
// 界面 5: 天机倾听与神思审度页 (语音输入与上传)
// ============================================================================
static void render_view_voice_listening(void) {
    build_header(s_screen, "天机倾听");

    lv_obj_t *card = lv_obj_create(s_screen);
    lv_obj_set_size(card, 216, 246);
    lv_obj_set_pos(card, 12, 36);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    tianji_voice_state_t st = tianji_voice_get_state();

    if (st == TIANJI_VOICE_STATE_UPLOADING) {
        lv_obj_t *title = lv_label_create(card);
        lv_obj_set_style_text_font(title, &tianji_font_16, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(COLOR_AMBER), 0);
        lv_label_set_text(title, "【神思审度中...】");
        lv_obj_set_pos(title, 0, 0);

        lv_obj_t *desc = lv_label_create(card);
        lv_obj_set_style_text_font(desc, &tianji_font_16, 0);
        lv_obj_set_style_text_color(desc, lv_color_hex(COLOR_WHITE), 0);
        lv_obj_set_width(desc, 192);
        lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
        lv_label_set_text(desc,
            "一念通天地，六爻动乾坤。\n\n"
            "天机网关正在推演卦象，请凝神静候宗师批断……");
        lv_obj_set_pos(desc, 0, 32);

        build_footer(s_screen, "神思审度中，请稍候");
    } else if (st == TIANJI_VOICE_STATE_ERROR) {
        lv_obj_t *title = lv_label_create(card);
        lv_obj_set_style_text_font(title, &tianji_font_16, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(COLOR_AMBER), 0);
        lv_label_set_text(title, "【推演受阻】");
        lv_obj_set_pos(title, 0, 0);

        lv_obj_t *desc = lv_label_create(card);
        lv_obj_set_style_text_font(desc, &tianji_font_16, 0);
        lv_obj_set_style_text_color(desc, lv_color_hex(COLOR_WHITE), 0);
        lv_obj_set_width(desc, 192);
        lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
        const char *err_text = tianji_voice_get_text_reply();
        if (!err_text || strlen(err_text) == 0) {
            err_text = "网络连接受阻，请检查 Wi-Fi 热点并稍后重试。";
        }
        lv_label_set_text(desc, err_text);
        lv_obj_set_pos(desc, 0, 32);

        build_footer(s_screen, "确定键返回总览");
    } else {
        // 正在录音倾听
        lv_obj_t *title = lv_label_create(card);
        lv_obj_set_style_text_font(title, &tianji_font_16, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(COLOR_CYAN), 0);
        lv_label_set_text(title, "【天机倾听中...】");
        lv_obj_set_pos(title, 0, 0);

        lv_obj_t *desc = lv_label_create(card);
        lv_obj_set_style_text_font(desc, &tianji_font_16, 0);
        lv_obj_set_style_text_color(desc, lv_color_hex(COLOR_WHITE), 0);
        lv_obj_set_width(desc, 192);
        lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
        lv_label_set_text(desc,
            "请面向掌机，直陈欲测之事\n"
            "（如求财、事业、姻缘等）：");
        lv_obj_set_pos(desc, 0, 30);

        // 动态音量波形/能量条指示
        int vol = tianji_voice_get_current_volume();
        lv_obj_t *bar_bg = lv_obj_create(card);
        lv_obj_set_size(bar_bg, 192, 14);
        lv_obj_set_pos(bar_bg, 0, 110);
        lv_obj_set_style_bg_color(bar_bg, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_style_border_width(bar_bg, 0, 0);
        lv_obj_set_style_radius(bar_bg, 3, 0);

        int bar_w = (vol * 192) / 100;
        if (bar_w < 6) bar_w = 6;
        if (bar_w > 192) bar_w = 192;
        lv_obj_t *bar_fill = lv_obj_create(bar_bg);
        lv_obj_set_size(bar_fill, bar_w, 14);
        lv_obj_set_pos(bar_fill, 0, 0);
        lv_obj_set_style_bg_color(bar_fill, lv_color_hex(COLOR_CYAN), 0);
        lv_obj_set_style_border_width(bar_fill, 0, 0);
        lv_obj_set_style_radius(bar_fill, 3, 0);

        lv_obj_t *hint = lv_label_create(card);
        lv_obj_set_style_text_font(hint, &tianji_font_16, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_GRAY), 0);
        lv_obj_set_width(hint, 192);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_label_set_text(hint, "轻启意念，声落卦应。\n说完请再按确定键。");
        lv_obj_set_pos(hint, 0, 140);

        build_footer(s_screen, "说完请再按确定键");
    }
}

// ============================================================================
// 界面 6: 意念未明引导页 (need_more_info)
// ============================================================================
static void render_view_voice_retry(void) {
    build_header(s_screen, "意念未明");

    lv_obj_t *card = lv_obj_create(s_screen);
    lv_obj_set_size(card, 216, 246);
    lv_obj_set_pos(card, 12, 36);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(card);
    lv_obj_set_style_text_font(title, &tianji_font_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_AMBER), 0);
    lv_label_set_text(title, "【天机提示】");
    lv_obj_set_pos(title, 0, 0);

    lv_obj_t *desc = lv_label_create(card);
    lv_obj_set_style_text_font(desc, &tianji_font_16, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(COLOR_WHITE), 0);
    lv_obj_set_width(desc, 192);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    const char *guide = tianji_voice_get_text_reply();
    if (!guide || strlen(guide) == 0) {
        guide = "卦无妄动，意在诚敬。所言未明心中所求，请指明欲测之事（如求财、事业、姻缘等），凝神后再禀。";
    }
    lv_label_set_text(desc, guide);
    lv_obj_set_pos(desc, 0, 28);

    build_footer(s_screen, "按确定键重新说话");
}

// ============================================================================
// 界面 7: 宗师神断呈现与语音播放页
// ============================================================================
static void render_view_ai(void) {
    build_header(s_screen, "宗师神断");

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
    lv_obj_set_style_text_color(content, lv_color_hex(COLOR_WHITE), 0);

    const char *res = tianji_voice_get_text_reply();
    if (!res || strlen(res) == 0) {
        res = tianji_net_get_ai_result();
    }

    if (res && strlen(res) > 0) {
        lv_label_set_text_fmt(content, "【天机神断】\n\n%s", res);
    } else {
        lv_label_set_text(content, "【天机隐匿】\n\n暂未感应到断语，请按确定键返回重试。");
    }

    if (tianji_voice_is_playing()) {
        build_footer(s_screen, "按确定键或下键静音阅读");
    } else {
        build_footer(s_screen, "上下键滚动，确定键返回");
    }
}

static void on_voice_status_update(tianji_voice_state_t state, const char *msg) {
    (void)msg;
    if (bsp_lvgl_lock(100)) {
        if (state == TIANJI_VOICE_STATE_NEED_MORE) {
            s_current_view = TIANJI_VIEW_VOICE_RETRY;
            tianji_audio_play(TIANJI_SFX_PAGE_FLIP);
        } else if (state == TIANJI_VOICE_STATE_SUCCESS) {
            s_current_view = TIANJI_VIEW_AI;
            tianji_audio_play(TIANJI_SFX_HEX_COMPLETE);
        }
        refresh_view();
        bsp_lvgl_unlock();
    }
}

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
    case TIANJI_VIEW_VOICE_LISTENING:
        render_view_voice_listening();
        break;
    case TIANJI_VIEW_VOICE_RETRY:
        render_view_voice_retry();
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
    ESP_LOGI(TAG, "天机六爻极简国风科技 UI 系统初始化完成");
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
            s_current_view = TIANJI_VIEW_STANDBY;
            tianji_audio_play(TIANJI_SFX_PAGE_FLIP);
            refresh_view();
        }
        break;

    case TIANJI_VIEW_OVERVIEW:
        if (action == TIANJI_KEY_OK_CLICK && tianji_net_is_enabled()) {
            // 联机模式下单按确定键：切入【天机倾听中...】启动语音解卦
            s_current_view = TIANJI_VIEW_VOICE_LISTENING;
            tianji_audio_play(TIANJI_SFX_START);
            refresh_view();
            tianji_voice_start(&s_session, on_voice_status_update);
        } else if (action == TIANJI_KEY_DOWN_CLICK || action == TIANJI_KEY_OK_CLICK) {
            s_current_view = TIANJI_VIEW_READING;
            s_reading_page = 0;
            tianji_audio_play(TIANJI_SFX_PAGE_FLIP);
            refresh_view();
        } else if (action == TIANJI_KEY_OK_LONG) {
            s_current_view = TIANJI_VIEW_STANDBY;
            tianji_audio_play(TIANJI_SFX_START);
            refresh_view();
        }
        break;

    case TIANJI_VIEW_VOICE_LISTENING:
        if (action == TIANJI_KEY_OK_CLICK) {
            if (tianji_voice_is_recording()) {
                // 用户说完再按确定键：手动结束录音，切入审度
                tianji_voice_stop_recording();
                refresh_view();
            } else if (tianji_voice_get_state() == TIANJI_VOICE_STATE_ERROR) {
                s_current_view = TIANJI_VIEW_OVERVIEW;
                tianji_audio_play(TIANJI_SFX_PAGE_FLIP);
                refresh_view();
            }
        } else if (action == TIANJI_KEY_OK_LONG) {
            tianji_voice_stop_recording();
            s_current_view = TIANJI_VIEW_STANDBY;
            tianji_audio_play(TIANJI_SFX_PAGE_FLIP);
            refresh_view();
        }
        break;

    case TIANJI_VIEW_VOICE_RETRY:
        if (action == TIANJI_KEY_OK_CLICK) {
            // 按确定键重新说话（当前卦象完整保留，无需重摇）
            tianji_voice_stop_playing();
            s_current_view = TIANJI_VIEW_VOICE_LISTENING;
            tianji_audio_play(TIANJI_SFX_START);
            refresh_view();
            tianji_voice_start(&s_session, on_voice_status_update);
        } else if (action == TIANJI_KEY_DOWN_CLICK) {
            tianji_voice_stop_playing();
            s_current_view = TIANJI_VIEW_OVERVIEW;
            tianji_audio_play(TIANJI_SFX_PAGE_FLIP);
            refresh_view();
        } else if (action == TIANJI_KEY_OK_LONG) {
            tianji_voice_stop_playing();
            s_current_view = TIANJI_VIEW_STANDBY;
            tianji_audio_play(TIANJI_SFX_PAGE_FLIP);
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
            s_current_view = TIANJI_VIEW_STANDBY;
            tianji_audio_play(TIANJI_SFX_START);
            refresh_view();
        }
        break;

    case TIANJI_VIEW_AI:
        if (tianji_voice_is_playing()) {
            // 播放过程中若按确定键或下键，立即静音打断，保留静默阅读
            if (action == TIANJI_KEY_OK_CLICK || action == TIANJI_KEY_DOWN_CLICK) {
                tianji_voice_stop_playing();
                refresh_view();
                return;
            }
        }

        if (action == TIANJI_KEY_DOWN_CLICK) {
            if (s_ai_card) lv_obj_scroll_by_bounded(s_ai_card, 0, -40, LV_ANIM_ON);
        } else if (action == TIANJI_KEY_UP_CLICK) {
            if (s_ai_card) lv_obj_scroll_by_bounded(s_ai_card, 0, 40, LV_ANIM_ON);
        } else if (action == TIANJI_KEY_OK_CLICK) {
            tianji_voice_stop_playing();
            s_current_view = TIANJI_VIEW_OVERVIEW;
            tianji_audio_play(TIANJI_SFX_PAGE_FLIP);
            refresh_view();
        } else if (action == TIANJI_KEY_OK_LONG) {
            tianji_voice_stop_playing();
            s_current_view = TIANJI_VIEW_STANDBY;
            tianji_audio_play(TIANJI_SFX_START);
            refresh_view();
        }
        break;

    default:
        break;
    }
}
