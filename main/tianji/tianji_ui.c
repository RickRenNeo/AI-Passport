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
#include "esp_timer.h"
#include "esp_random.h"
static uint32_t get_hardware_entropy(void) {
    return esp_random();
}
#else
#include <time.h>
static inline int64_t esp_timer_get_time(void) {
    return (int64_t)time(NULL) * 1000000LL;
}
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
static lv_obj_t *s_ai_label = NULL;
static lv_obj_t *s_footer_label = NULL;
static lv_obj_t *s_lbl_bat = NULL;
static lv_obj_t *s_lbl_net = NULL;
static lv_obj_t *s_lbl_volume = NULL;
static int s_battery_soc = -1;
static uint8_t s_reading_page = 0; // 0: 卦辞象曰, 1: 事业求财, 2: 姻缘变局
static int64_t s_ai_enter_us = 0; // 进入解卦页的绝对时间戳，用于按键防抖冷却

static void refresh_view(void);
static void on_voice_status_update(tianji_voice_state_t state, const char *msg);

static void update_net_label(lv_obj_t *lbl_net) {
    if (!lbl_net || !lv_obj_is_valid(lbl_net)) return;
    if (tianji_net_is_enabled()) {
        if (s_current_view == TIANJI_VIEW_AI) {
            tianji_voice_state_t vs = tianji_voice_get_state();
            if (vs == TIANJI_VOICE_STATE_RECORDING || vs == TIANJI_VOICE_STATE_CONNECTING) {
                lv_obj_set_style_text_color(lbl_net, lv_color_hex(COLOR_CYAN), 0);
                lv_label_set_text(lbl_net, "聆听");
            } else if (vs == TIANJI_VOICE_STATE_PLAYING) {
                lv_obj_set_style_text_color(lbl_net, lv_color_hex(COLOR_CYAN), 0);
                lv_label_set_text(lbl_net, "诵读");
            } else if (vs == TIANJI_VOICE_STATE_ERROR) {
                lv_obj_set_style_text_color(lbl_net, lv_color_hex(COLOR_AMBER), 0);
                lv_label_set_text(lbl_net, "受阻");
            } else {
                lv_obj_set_style_text_color(lbl_net, lv_color_hex(COLOR_AMBER), 0);
                lv_label_set_text(lbl_net, "推演");
            }
        } else if (tianji_net_is_config_mode()) {
            lv_obj_set_style_text_color(lbl_net, lv_color_hex(COLOR_AMBER), 0);
            lv_label_set_text(lbl_net, "配网");
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
}

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

    // 状态徽标
    lv_obj_t *lbl_net = lv_label_create(header);
    s_lbl_net = lbl_net;
    lv_obj_set_style_text_font(lbl_net, &tianji_font_16, 0);
    update_net_label(lbl_net);
    lv_obj_align(lbl_net, LV_ALIGN_TOP_RIGHT, -54, 4);

    // 右上角电量指示
    lv_obj_t *lbl_bat = lv_label_create(header);
    s_lbl_bat = lbl_bat;
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
    s_footer_label = lbl;
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
// 界面 1: 待命 / 准备起卦页（与上一版本完全一致）
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

    // 主标
    lv_obj_t *title = lv_label_create(card);
    lv_obj_set_style_text_font(title, &tianji_font_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_CYAN), 0);
    lv_label_set_text(title, "天机 · 六爻");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    // 心法提示
    lv_obj_t *tip = lv_label_create(card);
    lv_obj_set_style_text_font(tip, &tianji_font_16, 0);
    lv_obj_set_style_text_color(tip, lv_color_hex(COLOR_WHITE), 0);
    lv_label_set_text(tip, "凝神静虑，意在所求");
    lv_obj_align(tip, LV_ALIGN_TOP_MID, 0, 44);

    // 真实铜钱正反面图像展示箱
    lv_obj_t *coins_box = lv_obj_create(card);
    lv_obj_set_size(coins_box, 192, 60);
    lv_obj_align(coins_box, LV_ALIGN_TOP_MID, 0, 74);
    lv_obj_set_style_bg_color(coins_box, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_color(coins_box, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(coins_box, 1, 0);
    lv_obj_set_style_radius(coins_box, 4, 0);
    lv_obj_set_style_pad_all(coins_box, 0, 0);
    lv_obj_set_scrollbar_mode(coins_box, LV_SCROLLBAR_MODE_OFF);

    // 展示三枚铜钱真实图片（正面、背面、正面）
    lv_obj_t *c1 = lv_image_create(coins_box);
    lv_image_set_src(c1, &tianji_coin_front);
    lv_obj_set_pos(c1, 22, 13);

    lv_obj_t *c2 = lv_image_create(coins_box);
    lv_image_set_src(c2, &tianji_coin_back);
    lv_obj_set_pos(c2, 79, 13);

    lv_obj_t *c3 = lv_image_create(coins_box);
    lv_image_set_src(c3, &tianji_coin_front);
    lv_obj_set_pos(c3, 136, 13);

    // 装饰细线
    lv_obj_t *dline = lv_obj_create(card);
    lv_obj_set_size(dline, 160, 1);
    lv_obj_align(dline, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_set_style_bg_color(dline, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(dline, 0, 0);

    // 运行状态与模式说明
    lv_obj_t *mode_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(mode_lbl, &tianji_font_16, 0);
    lv_obj_set_style_text_color(mode_lbl, lv_color_hex(COLOR_GRAY), 0);
    lv_obj_set_style_text_align(mode_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(mode_lbl, 6, 0);
    if (tianji_net_is_config_mode()) {
        lv_label_set_text_fmt(mode_lbl, "配网模式: 请连接热点\nXiaozhi-%s\n访问 192.168.4.1", tianji_get_ap_suffix());
    } else if (tianji_net_is_enabled()) {
        lv_label_set_text(mode_lbl, "起卦法则：三钱古法\n运行模式：联机模式");
    } else {
        lv_label_set_text(mode_lbl, "起卦法则：三钱古法\n运行模式：脱机模式");
    }
    lv_obj_align(mode_lbl, LV_ALIGN_TOP_MID, 0, 166);

    // 音量指示：首页上下键可直接调节，这里常驻显示当前音量。
    if (!tianji_net_is_config_mode()) {
        lv_obj_t *vol_lbl = lv_label_create(card);
        s_lbl_volume = vol_lbl;
        lv_obj_set_style_text_font(vol_lbl, &tianji_font_16, 0);
        lv_obj_set_style_text_color(vol_lbl, lv_color_hex(COLOR_CYAN), 0);
        lv_obj_set_style_text_align(vol_lbl, LV_TEXT_ALIGN_CENTER, 0);
        int volume = tianji_volume_get();
        if (volume >= 0) {
            lv_label_set_text_fmt(vol_lbl, "音量 %d%%｜上下键调节", volume);
        } else {
            lv_label_set_text(vol_lbl, "音量 --%｜上下键调节");
        }
        lv_obj_align(vol_lbl, LV_ALIGN_TOP_MID, 0, 208);
    }

    build_footer(s_screen, "短按确定起卦  长按2秒切换模式");
}

// ============================================================================
// 界面 2: 逐次起卦推演页（与上一版本完全一致）
// ============================================================================
static void render_view_casting(void) {
    build_header(s_screen, "起卦推演");

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

        lv_obj_t *img0 = lv_image_create(coin_bar);
        lv_image_set_src(img0, t->coins[0] ? &tianji_coin_front : &tianji_coin_back);
        lv_obj_set_pos(img0, 10, 5);

        lv_obj_t *img1 = lv_image_create(coin_bar);
        lv_image_set_src(img1, t->coins[1] ? &tianji_coin_front : &tianji_coin_back);
        lv_obj_set_pos(img1, 48, 5);

        lv_obj_t *img2 = lv_image_create(coin_bar);
        lv_image_set_src(img2, t->coins[2] ? &tianji_coin_front : &tianji_coin_back);
        lv_obj_set_pos(img2, 86, 5);

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

            draw_yao_bar(yao_panel, 54, cur_y + 2, 98, 8, yang, col);

            if (moving) {
                lv_obj_t *m_lbl = lv_label_create(yao_panel);
                lv_obj_set_style_text_font(m_lbl, &tianji_font_16, 0);
                lv_obj_set_style_text_color(m_lbl, lv_color_hex(COLOR_AMBER), 0);
                lv_label_set_text(m_lbl, (ltype == TIANJI_LINE_LAOYANG) ? "阳动" : "阴动");
                lv_obj_set_pos(m_lbl, 164, cur_y - 2);
            }
        } else {
            lv_label_set_text(lbl, line_names[i]);
            lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_MUTED), 0);

            lv_obj_t *dim = lv_obj_create(yao_panel);
            lv_obj_set_size(dim, 98, 2);
            lv_obj_set_pos(dim, 54, cur_y + 5);
            lv_obj_set_style_bg_color(dim, lv_color_hex(COLOR_MUTED), 0);
            lv_obj_set_style_border_width(dim, 0, 0);

            if (i == s_session.current_line) {
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
// 界面 3: 卦象总览（原“卦象全息总览”已按要求更名为“卦象总览”）
// ============================================================================
static void render_view_overview(void) {
    const tianji_hexagram_t *prim = tianji_get_hexagram(s_session.primary_bits);
    const tianji_hexagram_t *trans = tianji_get_hexagram(s_session.transformed_bits);

    build_header(s_screen, "卦象总览");

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
        build_footer(s_screen, "确定键占卜问卦  下键看经文");
    } else {
        build_footer(s_screen, "下键看经文，长按2秒重起");
    }
}

// ============================================================================
// 界面 4: 经文详情页（按要求：左上角不显示卦名，只显示页码）
// ============================================================================
static void render_view_reading(void) {
    const tianji_hexagram_t *prim = tianji_get_hexagram(s_session.primary_bits);

    char hbuf[64];
    snprintf(hbuf, sizeof(hbuf), "第%d页 共3页", s_reading_page + 1);
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
// 界面 5: 天机解卦页（无键多轮语音问答 + 断语文字同步上屏）
// ============================================================================
static const char *voice_footer_prompt(void) {
    switch (tianji_voice_get_state()) {
    case TIANJI_VOICE_STATE_CONNECTING:
        return "宗师接通中，请稍候";
    case TIANJI_VOICE_STATE_RECORDING:
        return "● 静听中，说完自动呈报";
    case TIANJI_VOICE_STATE_UPLOADING:
        return "宗师推演中…";
    case TIANJI_VOICE_STATE_PLAYING:
        return tianji_voice_is_waiting_sentence() ? "宗师思索中，请稍候"
                                                  : "诵读中，确定键打断追问";
    case TIANJI_VOICE_STATE_ERROR:
        return "按确定键重试，长按退出";
    default:
        return "确定键继续问，长按退出";
    }
}

static void scroll_ai_to_bottom(void) {
    if (s_ai_card && lv_obj_is_valid(s_ai_card)) {
        lv_obj_update_layout(s_ai_card);
        lv_obj_scroll_by_bounded(s_ai_card, 0, -3000, LV_ANIM_OFF);
    }
}

static void render_view_ai(void) {
    build_header(s_screen, "天机解卦");

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
    s_ai_label = content;
    lv_obj_set_style_text_font(content, &tianji_font_16, 0);
    lv_obj_set_style_text_color(content, lv_color_hex(COLOR_WHITE), 0);
    lv_obj_set_width(content, 196);
    lv_label_set_long_mode(content, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(content, 0, 0);

    const char *text = tianji_voice_get_text_reply();
    if (text && text[0] != '\0') {
        lv_label_set_text(content, text);
    } else {
        lv_label_set_text(content, "宗师正在接通……");
    }

    build_footer(s_screen, voice_footer_prompt());
    scroll_ai_to_bottom();
}

static void update_ai_text_in_place(void) {
    if (!s_ai_label || !lv_obj_is_valid(s_ai_label)) {
        return;
    }
    const char *text = tianji_voice_get_text_reply();
    if (!text || text[0] == '\0') {
        text = "宗师正在接通……";
    }
    lv_label_set_text(s_ai_label, text);
    scroll_ai_to_bottom();
    if (s_footer_label && lv_obj_is_valid(s_footer_label)) {
        lv_label_set_text(s_footer_label, voice_footer_prompt());
    }
    if (s_lbl_net && lv_obj_is_valid(s_lbl_net)) {
        update_net_label(s_lbl_net);
    }
}

static void on_voice_status_update(tianji_voice_state_t state, const char *msg) {
    (void)msg;
    if (!bsp_lvgl_lock(200)) {
        return;
    }

    if (s_current_view != TIANJI_VIEW_AI) {
        s_current_view = TIANJI_VIEW_AI;
        refresh_view();
    } else {
        if (state == TIANJI_VOICE_STATE_ERROR) {
            tianji_audio_play(TIANJI_SFX_PAGE_FLIP);
        }
        // 开麦时不要再播放提示音：喇叭就在麦克风旁边，尾音会被当成"用户开口"，
        // 导致没说话也被提交一轮空录音。开麦只靠屏幕底部的"静听中"提示。
        update_ai_text_in_place();
    }

    bsp_lvgl_unlock();
}

static void refresh_view(void) {
    if (!s_screen) return;
    s_lbl_bat = NULL;
    s_lbl_net = NULL;
    s_lbl_volume = NULL;
    s_ai_card = NULL;
    s_ai_label = NULL;
    s_footer_label = NULL;
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

    // 直接接管当前物理活动屏幕，构建《天机·六爻》全屏国风界面
    s_screen = lv_screen_active();
    lv_obj_clean(s_screen);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_OFF);

    refresh_view();
    ESP_LOGI(TAG, "天机六爻 UI 系统初始化完成（已直接绑定主物理屏）");
}

void tianji_ui_destroy(void) {
    if (s_screen) {
        lv_obj_clean(s_screen);
        s_screen = NULL;
        s_ai_card = NULL;
        s_ai_label = NULL;
        s_footer_label = NULL;
        s_lbl_bat = NULL;
        s_lbl_net = NULL;
    }
}

void tianji_ui_update_battery(int soc) {
    if (s_battery_soc == soc) return;
    s_battery_soc = soc;
    if (s_lbl_bat && lv_obj_is_valid(s_lbl_bat)) {
        if (s_battery_soc >= 0) {
            lv_label_set_text_fmt(s_lbl_bat, "%d%%", s_battery_soc);
        } else {
            lv_label_set_text(s_lbl_bat, "--%");
        }
    }
}

void tianji_ui_update_net_state(tianji_net_state_t state) {
    (void)state;
    if (s_current_view == TIANJI_VIEW_STANDBY) {
        refresh_view();
    } else if (s_lbl_net && lv_obj_is_valid(s_lbl_net)) {
        update_net_label(s_lbl_net);
    }
}

tianji_view_t tianji_ui_get_current_view(void) {
    return s_current_view;
}

static void adjust_volume(int delta) {
    int volume = tianji_volume_step(delta);
    if (volume < 0) {
        return;
    }
    if (s_lbl_volume && lv_obj_is_valid(s_lbl_volume)) {
        lv_label_set_text_fmt(s_lbl_volume, "音量 %d%%｜上下键调节", volume);
    }
    tianji_audio_play(TIANJI_SFX_PAGE_FLIP);
}

void tianji_ui_handle_key(tianji_key_action_t action) {
    switch (s_current_view) {
    case TIANJI_VIEW_STANDBY:
        if (action == TIANJI_KEY_OK_CLICK) {
            tianji_session_init(&s_session);
            s_current_view = TIANJI_VIEW_CASTING;
            tianji_audio_play(TIANJI_SFX_START);
            refresh_view();
        } else if (action == TIANJI_KEY_UP_CLICK) {
            adjust_volume(10);
        } else if (action == TIANJI_KEY_DOWN_CLICK) {
            adjust_volume(-10);
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
        if (action == TIANJI_KEY_OK_CLICK) {
            // 单按确定键：直接进入解卦页并开始聆听，不再经过“请说话”中间页。
            if (!tianji_net_is_enabled()) {
                tianji_net_set_enabled(true);
            }
            s_current_view = TIANJI_VIEW_AI;
            s_ai_enter_us = esp_timer_get_time();
            refresh_view();
            tianji_voice_start(&s_session, on_voice_status_update);
        } else if (action == TIANJI_KEY_DOWN_CLICK) {
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

    case TIANJI_VIEW_AI: {
        tianji_voice_state_t st = tianji_voice_get_state();

        if (action == TIANJI_KEY_OK_LONG) {
            tianji_voice_end();
            s_current_view = TIANJI_VIEW_OVERVIEW;
            tianji_audio_play(TIANJI_SFX_PAGE_FLIP);
            refresh_view();
            break;
        }

        if (action == TIANJI_KEY_DOWN_CLICK) {
            if (s_ai_card) lv_obj_scroll_by_bounded(s_ai_card, 0, -40, LV_ANIM_ON);
            break;
        }
        if (action == TIANJI_KEY_UP_CLICK) {
            if (s_ai_card) lv_obj_scroll_by_bounded(s_ai_card, 0, 40, LV_ANIM_ON);
            break;
        }
        if (action != TIANJI_KEY_OK_CLICK) {
            break;
        }

        // 从总览页按下的那一下，按键释放/弹跳可能再送一个 CLICK，进入后 1.5 秒内忽略。
        const int64_t elapsed_ms = (esp_timer_get_time() - s_ai_enter_us) / 1000;
        if (elapsed_ms < 1500) {
            ESP_LOGI(TAG, "解卦页刚进入 (%d ms < 1500ms)，忽略本次按键", (int)elapsed_ms);
            break;
        }

        if (st == TIANJI_VOICE_STATE_RECORDING) {
            // 已说完，提前呈报（不说也可以继续说，静音 5 秒会自动呈报）。
            tianji_voice_stop_recording();
        } else if (st == TIANJI_VOICE_STATE_PLAYING) {
            // 打断诵读并立即开口追问（与小智原生交互一致）。
            tianji_voice_stop_playing();
            tianji_voice_followup();
            update_ai_text_in_place();
        } else if (st == TIANJI_VOICE_STATE_CONNECTING ||
                   st == TIANJI_VOICE_STATE_UPLOADING) {
            ESP_LOGI(TAG, "语音通道忙碌中，本次按键忽略");
        } else if (st == TIANJI_VOICE_STATE_NEED_MORE) {
            tianji_voice_followup();
        } else if (st == TIANJI_VOICE_STATE_ERROR) {
            // 出错后重新开一轮（保留已显示的问与答）。
            tianji_voice_followup();
        } else {
            // 空闲：直接继续追问，无需任何额外按键。
            s_ai_enter_us = esp_timer_get_time();
            tianji_voice_followup();
        }
        break;
    }

    default:
        break;
    }
}
