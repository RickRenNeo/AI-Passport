#include "ecard_ui.h"
#include "ecard_barcode.h"
#include "ecard_cards.h"
#include "ecard_flashcard.h"
#include "ecard_messages.h"
#include "ecard_netlink.h"
#include "ecard_pomodoro.h"
#include "ecard_portal.h"
#include "ecard_quiz.h"
#include "ecard_setup.h"
#include "ecard_voice.h"
#include <stdio.h>
#include <string.h>

#if defined(ESP_PLATFORM) || defined(LV_CONF_INCLUDE_SIMPLE) || defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#define HAS_LVGL_RUNTIME 1
#else
#define HAS_LVGL_RUNTIME 0
typedef void *lv_obj_t;
typedef struct lv_font_t lv_font_t;
#endif

/* ------------------------------------------------------------------------- */
/* 设计基线                                                                  */
/* ------------------------------------------------------------------------- */

#define ECARD_IDLE_TIMEOUT_MS (120 * 1000)

#define C_BG        0xEDF3F9
#define C_BG_BOT    0xE2EBF6
#define C_SURFACE   0xFFFFFF
#define C_INK       0x0F2744
#define C_MUTED     0x5B7089
#define C_FAINT     0x93A6BC
#define C_LINE      0xE1E9F2
#define C_BLUE      0x2563EB
#define C_BLUE_SOFT 0xE8F0FE
#define C_AMBER     0xF59E0B
#define C_AMBER_BG  0xFFF7E6
#define C_GREEN     0x10B981
#define C_RED       0xDC2626
#define C_RED_SOFT  0xFFF3F3
#define C_NAVY      0x0B2240
#define C_NAVY_HI   0x1A4675
#define C_NAVY_SOFT 0x1E3A5F
#define C_NAVY_LINE 0x2C537E
#define C_ON_NAVY   0xBFD5EC
#define NO_BORDER   0xFFFFFFFFu

/* ------------------------------------------------------------------------- */
/* 跨模式状态                                                                */
/* ------------------------------------------------------------------------- */

static ecard_ui_page_t s_page = ECARD_UI_PAGE_HOME;
static ecard_ui_modal_t s_modal = ECARD_UI_MODAL_NONE;
static uint8_t s_menu_selection = 0;
static uint8_t s_msg_selection = 0;
static uint8_t s_grade_selection = 0;
static uint8_t s_deck_grade = 1;
static uint32_t s_idle_elapsed_ms = 0;
static bool s_is_dimmed = false;
static char s_time_str[8] = "09:40";
static int s_battery_pct = -1;
static bool s_is_charging = false;
static bool s_wifi_connected = false;
/* 小智 AI 会话是否已就绪：状态栏绿点提示“现在可以长按 UP 追问”。 */
static bool s_ai_connected = false;
// 设备页“长按 UP 重试”的瞬时状态：重连发起后先显示“正在重新连接”，
// 连上或超时后回到常规文案。
static bool s_net_retrying = false;

#if HAS_LVGL_RUNTIME

LV_FONT_DECLARE(ecard_font_9);
LV_FONT_DECLARE(ecard_font_12);
LV_FONT_DECLARE(ecard_font_16);
LV_FONT_DECLARE(ecard_font_22);
LV_FONT_DECLARE(ecard_font_48);

#include "ecard_photo.h"

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_lbl_time = NULL;
static lv_obj_t *s_lbl_battery = NULL;
static lv_obj_t *s_battery_fill = NULL;
static lv_obj_t *s_wifi_bars[4];
/* 状态栏小智 AI 指示灯：蓝点=已连上，灰点=未连接/未使用。 */
static lv_obj_t *s_ai_dot = NULL;
static lv_obj_t *s_lbl_ai = NULL;

/* 首页 */
static lv_obj_t *s_panel_identity = NULL;
static lv_obj_t *s_lbl_name_class = NULL;
static lv_obj_t *s_badge_capsule = NULL;
static lv_obj_t *s_lbl_badge_text = NULL;
static lv_obj_t *s_panel_unread = NULL;
static lv_obj_t *s_lbl_unread_title = NULL;
static lv_obj_t *s_lbl_unread_action = NULL;
static lv_obj_t *s_lbl_hint = NULL;

/* 菜单 */
static lv_obj_t *s_menu_rows[ECARD_MENU_COUNT];
static lv_obj_t *s_menu_tiles[ECARD_MENU_COUNT];
static lv_obj_t *s_menu_titles[ECARD_MENU_COUNT];
static lv_obj_t *s_menu_subs[ECARD_MENU_COUNT];
static lv_obj_t *s_menu_glyphs[ECARD_MENU_COUNT];
static lv_obj_t *s_menu_chevrons[ECARD_MENU_COUNT];
static lv_obj_t *s_lbl_menu_voice_sub = NULL;

/* 留言 */
static lv_obj_t *s_msg_list = NULL;
static lv_obj_t *s_msg_empty_panel = NULL;
static lv_obj_t *s_msg_rows[ECARD_MESSAGES_MAX];

/* 设备 */
static lv_obj_t *s_lbl_dev_battery = NULL;
static lv_obj_t *s_bar_dev_battery = NULL;
static lv_obj_t *s_lbl_dev_network = NULL;
static lv_obj_t *s_dev_note = NULL;
static lv_obj_t *s_dev_note_badge = NULL;
static lv_obj_t *s_lbl_dev_note_title = NULL;
static lv_obj_t *s_lbl_dev_note_sub = NULL;

/* 首次配网：记录当前渲染的阶段，状态变化时整页重绘。 */
static int s_setup_stage = -999;

/* 随堂答题 */
static lv_obj_t *s_quiz_body = NULL;
static lv_obj_t *s_quiz_result = NULL;
static lv_obj_t *s_lbl_quiz_progress = NULL;
static lv_obj_t *s_quiz_bar = NULL;
static lv_obj_t *s_lbl_quiz_prompt = NULL;
static lv_obj_t *s_quiz_options[ECARD_QUIZ_OPTION_COUNT];
static lv_obj_t *s_quiz_badges[ECARD_QUIZ_OPTION_COUNT];
static lv_obj_t *s_lbl_quiz_letters[ECARD_QUIZ_OPTION_COUNT];
static lv_obj_t *s_lbl_quiz_options[ECARD_QUIZ_OPTION_COUNT];
static lv_obj_t *s_lbl_quiz_feedback = NULL;
static lv_obj_t *s_lbl_quiz_score = NULL;
static lv_obj_t *s_lbl_quiz_caption = NULL;
static lv_obj_t *s_lbl_quiz_stars = NULL;

/* 年级选择 */
static lv_obj_t *s_grade_rows[ECARD_GRADE_COUNT];
static lv_obj_t *s_grade_tiles[ECARD_GRADE_COUNT];
static lv_obj_t *s_grade_glyphs[ECARD_GRADE_COUNT];
static lv_obj_t *s_grade_titles[ECARD_GRADE_COUNT];
static lv_obj_t *s_grade_subs[ECARD_GRADE_COUNT];

/* 番茄钟 */
static lv_obj_t *s_pomo_phase = NULL;
static lv_obj_t *s_pomo_time = NULL;
static lv_obj_t *s_pomo_bar = NULL;
static lv_obj_t *s_pomo_round = NULL;
static lv_obj_t *s_pomo_tip = NULL;

/* 闪卡 */
static lv_obj_t *s_fc_body = NULL;
static lv_obj_t *s_fc_result = NULL;
static lv_obj_t *s_lbl_fc_progress = NULL;
static lv_obj_t *s_fc_kind = NULL;
static lv_obj_t *s_lbl_fc_kind = NULL;
static lv_obj_t *s_fc_face = NULL;
static lv_obj_t *s_lbl_fc_front = NULL;
static lv_obj_t *s_fc_divider = NULL;
static lv_obj_t *s_lbl_fc_back = NULL;
static lv_obj_t *s_lbl_fc_source = NULL;
static lv_obj_t *s_lbl_fc_score = NULL;
static lv_obj_t *s_lbl_fc_caption = NULL;
static lv_obj_t *s_lbl_fc_explain = NULL;
static lv_obj_t *s_lbl_fc_example = NULL;
static lv_obj_t *s_lbl_fc_example_cn = NULL;

/* 模态层 */
static lv_obj_t *s_modal_container = NULL;
static lv_obj_t *s_lbl_modal_big = NULL;
static lv_obj_t *s_lbl_modal_kicker = NULL;
static lv_obj_t *s_lbl_modal_title = NULL;
static lv_obj_t *s_lbl_modal_sub = NULL;
static lv_obj_t *s_lbl_modal_hint = NULL;
static lv_obj_t *s_bar_modal = NULL;
static lv_obj_t *s_wave_bars[11];
static int s_wave_pattern[11] = {38, 66, 90, 58, 100, 72, 46, 84, 60, 96, 54};

/* ------------------------------------------------------------------------- */
/* 基础控件                                                                  */
/* ------------------------------------------------------------------------- */

static lv_color_t color_from_rgb565(uint16_t value) {
    uint8_t r = (uint8_t)((value >> 11) & 0x1F);
    uint8_t g = (uint8_t)((value >> 5) & 0x3F);
    uint8_t b = (uint8_t)(value & 0x1F);
    r = (uint8_t)((r << 3) | (r >> 2));
    g = (uint8_t)((g << 2) | (g >> 4));
    b = (uint8_t)((b << 3) | (b >> 2));
    return lv_color_make(r, g, b);
}

static lv_obj_t *make_box(lv_obj_t *parent, int x, int y, int w, int h,
                          uint32_t bg, uint32_t border, int radius) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    if (border != NO_BORDER) {
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
        lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_border_width(obj, 0, 0);
    }
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static const lv_font_t *fit_label_font(const char *text, int width,
                                       const lv_font_t *const *candidates, int count) {
    lv_point_t size;
    for (int i = 0; i < count; ++i) {
        lv_text_get_size(&size, text, candidates[i], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x <= width) return candidates[i];
    }
    return candidates[count - 1];
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            uint32_t color, int x, int y, int w, lv_text_align_t align) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    /* 固定尺寸的标签一律单行裁切：宁可截断，也不让一行文案意外折行破坏排版。 */
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    if (w > 0) {
        lv_obj_set_width(label, w);
        lv_obj_set_style_text_align(label, align, 0);
    }
    lv_obj_set_pos(label, x, y);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

static void label_set_text(lv_obj_t *label, const char *text) {
    if (!label || !text) return;
    const char *old = lv_label_get_text(label);
    if (!old || strcmp(old, text) != 0) {
        lv_label_set_text(label, text);
    }
}

/* 题库题面可能占两到三行，使用换行模式并锁定高度，避免撑破卡片。 */
static lv_obj_t *make_wrapped_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                                    uint32_t color, int x, int y, int w, int h,
                                    lv_text_align_t align) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(label, w, h);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_pos(label, x, y);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

/* 证件卡内可用宽度有限，去掉星标以保证单行显示。 */
static const char *role_badge_short(ecard_role_t role) {
    switch (role) {
    case ECARD_ROLE_STUDENT: return "学生 · STUDENT";
    case ECARD_ROLE_FACULTY: return "教师 · FACULTY";
    case ECARD_ROLE_STAFF:
    default: return "教职工 · STAFF";
    }
}

static uint32_t battery_color(void) {
    if (s_battery_pct < 0 || s_battery_pct > 100) return C_FAINT;
    if (s_battery_pct <= 15) return C_RED;
    if (s_battery_pct <= 30) return C_AMBER;
    return C_GREEN;
}

static void format_battery(char *buf, size_t len) {
    if (s_battery_pct < 0 || s_battery_pct > 100) {
        snprintf(buf, len, "--");
    } else if (s_is_charging) {
        snprintf(buf, len, "%d%%+", s_battery_pct);
    } else {
        snprintf(buf, len, "%d%%", s_battery_pct);
    }
}

/* 证件照：白色相框 + 圆角裁切 + RGB565 人像资源。 */
static lv_obj_t *create_photo(lv_obj_t *parent, int x, int y, int w, int h, int radius,
                              const lv_image_dsc_t *src) {
    lv_obj_t *frame = make_box(parent, x, y, w, h, C_SURFACE, C_LINE, radius);

    lv_obj_t *inner = make_box(frame, 2, 2, w - 4, h - 4, C_SURFACE, NO_BORDER,
                               radius > 4 ? radius - 2 : 0);

    lv_obj_t *img = lv_image_create(inner);
    lv_image_set_src(img, src);
    lv_obj_set_pos(img, 0, 0);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    return frame;
}

/* ------------------------------------------------------------------------- */
/* 条形码绘制                                                                */
/* ------------------------------------------------------------------------- */

/* 首页左侧竖向条码：模块沿 Y 轴排列，每根条横向铺满卡片，读起来像旋转 90° 的标签。 */
static void draw_barcode_vertical(lv_event_t *event) {
    lv_obj_t *obj = lv_event_get_target_obj(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    if (!obj || !layer) return;

    uint8_t modules[ECARD_BARCODE_MAX_MODULES];
    size_t count = 0;
    const ecard_profile_t *prof = ecard_profile_get();
    if (!ecard_barcode_encode_code128_auto(prof->student_id, modules, sizeof(modules), &count)) {
        return;
    }

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    int32_t height = coords.y2 - coords.y1 + 1;
    int32_t x1 = coords.x1 + 1;
    int32_t x2 = coords.x2 - 1;
    if (height <= 0 || x2 <= x1) return;

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.bg_color = lv_color_hex(0x0F172A);
    dsc.radius = 0;
    dsc.border_width = 0;

    size_t i = 0;
    while (i < count) {
        if (modules[i] == 0) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < count && modules[j] == 1) ++j;
        int32_t y1 = coords.y1 + (int32_t)((i * (size_t)height) / count);
        int32_t y2 = coords.y1 + (int32_t)((j * (size_t)height) / count) - 1;
        if (y2 < y1) y2 = y1;
        lv_area_t area = {x1, y1, x2, y2};
        lv_draw_rect(layer, &dsc, &area);
        i = j;
    }
}

/* ------------------------------------------------------------------------- */
/* 页面公共部分                                                              */
/* ------------------------------------------------------------------------- */

static void reset_page_pointers(void) {
    s_lbl_time = NULL;
    s_lbl_battery = NULL;
    s_ai_dot = NULL;
    s_lbl_ai = NULL;
    s_battery_fill = NULL;
    memset(s_wifi_bars, 0, sizeof(s_wifi_bars));
    s_panel_identity = NULL;
    s_lbl_name_class = NULL;
    s_badge_capsule = NULL;
    s_lbl_badge_text = NULL;
    s_panel_unread = NULL;
    s_lbl_unread_title = NULL;
    s_lbl_unread_action = NULL;
    s_lbl_hint = NULL;
    memset(s_menu_rows, 0, sizeof(s_menu_rows));
    memset(s_menu_tiles, 0, sizeof(s_menu_tiles));
    memset(s_menu_titles, 0, sizeof(s_menu_titles));
    memset(s_menu_subs, 0, sizeof(s_menu_subs));
    memset(s_menu_glyphs, 0, sizeof(s_menu_glyphs));
    memset(s_menu_chevrons, 0, sizeof(s_menu_chevrons));
    s_lbl_menu_voice_sub = NULL;
    s_msg_list = NULL;
    s_msg_empty_panel = NULL;
    memset(s_msg_rows, 0, sizeof(s_msg_rows));
    s_msg_selection = 0;
    s_lbl_dev_battery = NULL;
    s_bar_dev_battery = NULL;
    s_lbl_dev_network = NULL;
    s_dev_note = NULL;
    s_dev_note_badge = NULL;
    s_lbl_dev_note_title = NULL;
    s_lbl_dev_note_sub = NULL;
    s_quiz_body = NULL;
    s_quiz_result = NULL;
    s_lbl_quiz_progress = NULL;
    s_quiz_bar = NULL;
    s_lbl_quiz_prompt = NULL;
    memset(s_quiz_options, 0, sizeof(s_quiz_options));
    memset(s_quiz_badges, 0, sizeof(s_quiz_badges));
    memset(s_lbl_quiz_letters, 0, sizeof(s_lbl_quiz_letters));
    memset(s_lbl_quiz_options, 0, sizeof(s_lbl_quiz_options));
    s_lbl_quiz_feedback = NULL;
    s_lbl_quiz_score = NULL;
    s_lbl_quiz_caption = NULL;
    s_lbl_quiz_stars = NULL;
    s_fc_body = NULL;
    s_fc_result = NULL;
    s_lbl_fc_progress = NULL;
    s_fc_kind = NULL;
    s_lbl_fc_kind = NULL;
    s_fc_face = NULL;
    s_lbl_fc_front = NULL;
    s_fc_divider = NULL;
    s_lbl_fc_back = NULL;
    s_lbl_fc_source = NULL;
    s_lbl_fc_score = NULL;
    s_lbl_fc_caption = NULL;
    memset(s_grade_rows, 0, sizeof(s_grade_rows));
    memset(s_grade_tiles, 0, sizeof(s_grade_tiles));
    memset(s_grade_glyphs, 0, sizeof(s_grade_glyphs));
    memset(s_grade_titles, 0, sizeof(s_grade_titles));
    memset(s_grade_subs, 0, sizeof(s_grade_subs));
    s_lbl_fc_explain = NULL;
    s_lbl_fc_example = NULL;
    s_lbl_fc_example_cn = NULL;
    s_pomo_phase = NULL;
    s_pomo_time = NULL;
    s_pomo_bar = NULL;
    s_pomo_round = NULL;
    s_pomo_tip = NULL;
    s_modal_container = NULL;
    s_lbl_modal_big = NULL;
    s_lbl_modal_kicker = NULL;
    s_lbl_modal_title = NULL;
    s_lbl_modal_sub = NULL;
    s_lbl_modal_hint = NULL;
    s_bar_modal = NULL;
    memset(s_wave_bars, 0, sizeof(s_wave_bars));
}

static lv_obj_t *create_page_root(void) {
    lv_obj_t *root = lv_obj_create(NULL);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 240, 320);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_grad_color(root, lv_color_hex(C_BG_BOT), 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    return root;
}

static void create_battery_icon(lv_obj_t *parent, int x, int y) {
    lv_obj_t *body = make_box(parent, x, y, 18, 10, C_SURFACE, C_FAINT, 2);
    lv_obj_set_style_border_width(body, 1, 0);
    make_box(parent, x + 18, y + 3, 2, 4, C_FAINT, NO_BORDER, 1);
    s_battery_fill = make_box(parent, x + 2, y + 2, 14, 6, battery_color(), NO_BORDER, 1);
}

static void update_battery_icon(void) {
    if (!s_battery_fill) return;
    if (s_battery_pct < 0 || s_battery_pct > 100) {
        lv_obj_add_flag(s_battery_fill, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_battery_fill, LV_OBJ_FLAG_HIDDEN);
    int width = (s_battery_pct * 14) / 100;
    if (width < 1 && s_battery_pct > 0) width = 1;
    lv_obj_set_width(s_battery_fill, width);
    lv_obj_set_style_bg_color(s_battery_fill, lv_color_hex(battery_color()), 0);
}

/* 统一状态栏：时间左对齐；右侧依次为信号、电量文字与电池图标，统一 16 px 页边距。 */
static void create_wifi_icon(lv_obj_t *parent, int x, int y) {
    static const int heights[4] = {4, 7, 10, 13};
    uint32_t color = s_wifi_connected ? C_BLUE : C_FAINT;
    lv_opa_t opa = s_wifi_connected ? LV_OPA_COVER : LV_OPA_50;
    for (int i = 0; i < 4; ++i) {
        s_wifi_bars[i] = make_box(parent, x + i * 4, y + 13 - heights[i], 3, heights[i],
                                  color, NO_BORDER, 1);
        lv_obj_set_style_bg_opa(s_wifi_bars[i], opa, 0);
    }
}

static void update_wifi_icon(void) {
    uint32_t color = s_wifi_connected ? C_BLUE : C_FAINT;
    lv_opa_t opa = s_wifi_connected ? LV_OPA_COVER : LV_OPA_50;
    for (int i = 0; i < 4; ++i) {
        if (!s_wifi_bars[i]) continue;
        lv_obj_set_style_bg_color(s_wifi_bars[i], lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(s_wifi_bars[i], opa, 0);
    }
}

static void create_ai_icon(lv_obj_t *parent) {
    s_ai_dot = make_box(parent, 92, 9, 7, 7, C_FAINT, NO_BORDER, LV_RADIUS_CIRCLE);
    s_lbl_ai = make_label(parent, "AI", &ecard_font_9, C_FAINT, 101, 6, 20,
                          LV_TEXT_ALIGN_LEFT);
}

static void update_ai_icon(void) {
    const uint32_t color = s_ai_connected ? 0x2E9E6B : C_FAINT;
    if (s_ai_dot) {
        lv_obj_set_style_bg_color(s_ai_dot, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(s_ai_dot, s_ai_connected ? LV_OPA_COVER : LV_OPA_40, 0);
    }
    if (s_lbl_ai) {
        lv_obj_set_style_text_color(s_lbl_ai, lv_color_hex(s_ai_connected ? C_BLUE : C_FAINT), 0);
    }
}

static void create_status_bar(lv_obj_t *root) {
    char bat[16];
    format_battery(bat, sizeof(bat));
    s_lbl_time = make_label(root, s_time_str, &ecard_font_16, C_INK, 8, 2, 84,
                            LV_TEXT_ALIGN_LEFT);
    create_ai_icon(root);
    s_lbl_battery = make_label(root, bat, &ecard_font_12, C_MUTED, 146, 6, 54,
                                LV_TEXT_ALIGN_RIGHT);
    create_wifi_icon(root, 124, 6);
    create_battery_icon(root, 204, 7);
    update_battery_icon();
    update_wifi_icon();
    update_ai_icon();
}

/* 标题与一行功能说明共用同一行：说明紧跟标题右侧，把纵向空间让给内容。 */
static void create_page_header(lv_obj_t *root, const char *title, const char *subtitle) {
    create_status_bar(root);
    lv_obj_t *lbl_title = make_label(root, title, &ecard_font_22, C_INK, 16, 26, 0,
                                     LV_TEXT_ALIGN_LEFT);
    lv_obj_set_width(lbl_title, LV_SIZE_CONTENT);
    if (subtitle) {
        /* 先让 LVGL 完成标题的自适应宽度，再据实宽摆放说明，绝不压出右边界。 */
        lv_obj_update_layout(root);
        const int32_t left = 16 + lv_obj_get_width(lbl_title) + 8;
        int32_t width = 240 - 8 - left;
        if (width < 24) width = 24;
        const int32_t y = 26 + lv_obj_get_height(lbl_title) - 14 - 2;
        make_label(root, subtitle, &ecard_font_12, C_MUTED, left, y, width,
                   LV_TEXT_ALIGN_LEFT);
    }
}

static void create_page_hint(lv_obj_t *root, const char *text) {
    /* 页面底部提示统一登记到 s_lbl_hint，结算面板才能就地改文案。 */
    s_lbl_hint = make_label(root, text, &ecard_font_12, C_FAINT, 8, 302, 224,
                            LV_TEXT_ALIGN_CENTER);
}

/* ------------------------------------------------------------------------- */
/* 首页                                                                      */
/* ------------------------------------------------------------------------- */

static void create_home_page(void) {
    const ecard_profile_t *prof = ecard_profile_get();
    s_screen = create_page_root();

    /* 状态栏 */
    create_status_bar(s_screen);

    /* 居中校名 + 品牌金线：顶部只保留学校身份，不与下方证卡抢焦点。 */
    static const lv_font_t *const school_fonts[] = { &ecard_font_16, &ecard_font_12 };
    make_label(s_screen, prof->school_name,
               fit_label_font(prof->school_name, 200, school_fonts, 2), C_INK, 20, 26, 200,
               LV_TEXT_ALIGN_CENTER);
    make_box(s_screen, 106, 50, 28, 3, C_AMBER, NO_BORDER, 2);

    /* 左侧竖向条码卡：白色窄卡 + 学号，与右侧大照片形成证书式双栏。 */
    lv_obj_t *barcode_card = make_box(s_screen, 22, 58, 50, 188, C_SURFACE, C_LINE, 14);
    lv_obj_t *barcode = make_box(barcode_card, 7, 8, 36, 156, C_SURFACE, NO_BORDER, 0);
    lv_obj_add_event_cb(barcode, draw_barcode_vertical, LV_EVENT_DRAW_MAIN, NULL);
    make_label(barcode_card, prof->student_id, &ecard_font_9, C_MUTED, 2, 167, 46,
               LV_TEXT_ALIGN_CENTER);

    /* 右侧大证件照：白框压住浅蓝背景，圆角与竖条卡呼应。 */
    create_photo(s_screen, 84, 58, 134, 188, 14, &ecard_portrait_home);

    /* 身份信息与未读留言互斥，占用同一块 204 × 52 区域。
       身份态不铺卡片，让姓名直接落在渐变背景上，避免和照片白框粘连。 */
    s_panel_identity = make_box(s_screen, 18, 246, 204, 52, C_BG, NO_BORDER, 0);
    lv_obj_set_style_bg_opa(s_panel_identity, LV_OPA_TRANSP, 0);
    char name_class[96];
    snprintf(name_class, sizeof(name_class), "%s · %s", prof->student_name, prof->class_name);
    static const lv_font_t *const name_fonts[] = { &ecard_font_22, &ecard_font_16,
                                                   &ecard_font_12 };
    s_lbl_name_class = make_label(s_panel_identity, name_class,
                                  fit_label_font(name_class, 204, name_fonts, 3), C_INK,
                                  0, 2, 204, LV_TEXT_ALIGN_CENTER);
    s_badge_capsule = make_box(s_panel_identity, 37, 30, 130, 20,
                               prof->theme.bg_color_rgb565, NO_BORDER, 10);
    lv_obj_set_style_border_width(s_badge_capsule, 1, 0);
    lv_obj_set_style_border_color(s_badge_capsule,
                                  color_from_rgb565(prof->theme.border_color_rgb565), 0);
    make_box(s_badge_capsule, 12, 7, 6, 6, C_GREEN, NO_BORDER, 3);
    s_lbl_badge_text = make_label(s_badge_capsule, role_badge_short(prof->role), &ecard_font_12,
                                  C_MUTED, 22, 2, 96, LV_TEXT_ALIGN_LEFT);

    s_panel_unread = make_box(s_screen, 18, 246, 204, 52, C_AMBER_BG, 0xF0D6A2, 14);
    make_box(s_panel_unread, 0, 5, 4, 42, C_AMBER, NO_BORDER, 2);
    s_lbl_unread_title = make_label(s_panel_unread, "", &ecard_font_16, 0x92400E, 16, 5, 176,
                                    LV_TEXT_ALIGN_LEFT);
    s_lbl_unread_action = make_label(s_panel_unread, "", &ecard_font_12, 0xB45309, 16, 30, 176,
                                     LV_TEXT_ALIGN_LEFT);

    s_lbl_hint = make_label(s_screen, "", &ecard_font_12, C_FAINT, 8, 300, 224,
                            LV_TEXT_ALIGN_CENTER);

    /* 主题色需要从 RGB565 还原为 RGB888 后再使用 */
    lv_obj_set_style_bg_color(s_badge_capsule, color_from_rgb565(prof->theme.bg_color_rgb565), 0);
    lv_obj_set_style_text_color(s_lbl_badge_text, color_from_rgb565(prof->theme.text_color_rgb565), 0);

    update_battery_icon();
}

/* ------------------------------------------------------------------------- */
/* 快捷菜单                                                                  */
/* ------------------------------------------------------------------------- */

static void apply_menu_selection(void) {
    for (int i = 0; i < ECARD_MENU_COUNT; ++i) {
        lv_obj_t *row = s_menu_rows[i];
        if (!row) continue;
        bool selected = (i == (int)s_menu_selection);

        lv_obj_set_style_border_width(row, selected ? 0 : 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(C_LINE), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(selected ? C_NAVY : C_SURFACE), 0);
        lv_obj_set_style_bg_grad_color(row, lv_color_hex(C_NAVY_HI), 0);
        lv_obj_set_style_bg_grad_dir(row, selected ? LV_GRAD_DIR_VER : LV_GRAD_DIR_NONE, 0);

        if (s_menu_tiles[i]) {
            lv_obj_set_style_bg_color(s_menu_tiles[i],
                                      lv_color_hex(selected ? 0x2A517C : C_BLUE_SOFT), 0);
        }
        if (s_menu_glyphs[i]) {
            lv_obj_set_style_text_color(s_menu_glyphs[i],
                                        lv_color_hex(selected ? C_SURFACE : C_BLUE), 0);
        }
        if (s_menu_titles[i]) {
            lv_obj_set_style_text_color(s_menu_titles[i],
                                        lv_color_hex(selected ? C_SURFACE : C_INK), 0);
        }
        if (s_menu_subs[i]) {
            lv_obj_set_style_text_color(s_menu_subs[i],
                                        lv_color_hex(selected ? C_ON_NAVY : C_MUTED), 0);
        }
        if (s_menu_chevrons[i]) {
            lv_obj_set_style_text_color(s_menu_chevrons[i],
                                        lv_color_hex(selected ? C_ON_NAVY : C_FAINT), 0);
        }
    }
}

static void create_menu_page(void) {
    s_screen = create_page_root();
    create_page_header(s_screen, "快捷菜单", "OK 进入");

    static const char *titles[ECARD_MENU_COUNT] = {"语音留言", "随堂答题", "古诗接句",
                                                   "英语单词", "番茄钟", "设备信息"};
    static const char *tile_chars[ECARD_MENU_COUNT] = {"话", "题", "诗", "词", "钟", "机"};

    /* 六项菜单用 34 px 紧凑行，只保留标题：62 + 5 × 34 + 32 = 264。 */
    for (int i = 0; i < ECARD_MENU_COUNT; ++i) {
        int y = 62 + i * 34;
        lv_obj_t *row = make_box(s_screen, 16, y, 208, 32, C_SURFACE, C_LINE, 10);
        lv_obj_t *tile = make_box(row, 7, 3, 26, 26, C_BLUE_SOFT, NO_BORDER, 8);
        lv_obj_t *glyph = make_label(tile, tile_chars[i], &ecard_font_16, C_BLUE, 0, 5, 26,
                                     LV_TEXT_ALIGN_CENTER);
        lv_obj_t *title = make_label(row, titles[i], &ecard_font_16, C_INK, 42, 5, 130,
                                     LV_TEXT_ALIGN_LEFT);
        lv_obj_t *chevron = make_label(row, "›", &ecard_font_16, C_FAINT, 186, 7, 16,
                                        LV_TEXT_ALIGN_CENTER);

        s_menu_rows[i] = row;
        s_menu_tiles[i] = tile;
        s_menu_glyphs[i] = glyph;
        s_menu_titles[i] = title;
        s_menu_subs[i] = NULL;
        s_menu_chevrons[i] = chevron;
        if (i == ECARD_MENU_VOICE) {
            s_lbl_menu_voice_sub = NULL;
        }
    }

    create_page_hint(s_screen, "UP / DOWN 选择   ·   长按 OK 返回首页");
    apply_menu_selection();
}

/* ------------------------------------------------------------------------- */
/* 随堂答题                                                                  */
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/* 随堂答题                                                                  */
/* ------------------------------------------------------------------------- */

#define QUIZ_PROMPT_W 184
#define QUIZ_PROMPT_MAX_H 64
#define QUIZ_OPTION_TEXT_W 144
#define QUIZ_OPTIONS_BOTTOM 212

/* 用 LVGL 自身的换行测量，让长题面/长选项按实际像素高度自动降字号。 */
static int32_t quiz_text_height(const char *text, const lv_font_t *font, int32_t width) {
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, width, LV_TEXT_FLAG_NONE);
    return size.y;
}

static const lv_font_t *quiz_fit_font(const char *text, int32_t width, int32_t max_h,
                                      int32_t *out_h) {
    const lv_font_t *font = &ecard_font_16;
    int32_t h = quiz_text_height(text, font, width);
    if (h > max_h) {
        font = &ecard_font_12;
        h = quiz_text_height(text, font, width);
    }
    if (out_h) *out_h = h;
    return font;
}

static void create_quiz_page(void) {
    s_screen = create_page_root();
    create_page_header(s_screen, "随堂答题", "一年级综合练习");

    lv_obj_t *card = make_box(s_screen, 16, 66, 208, 224, C_SURFACE, C_LINE, 18);

    /* 答题主体：进度、题面、A/B/C 三个选项与判题反馈。 */
    s_quiz_body = make_box(card, 0, 0, 208, 216, C_SURFACE, NO_BORDER, 0);
    lv_obj_set_style_bg_opa(s_quiz_body, LV_OPA_TRANSP, 0);

    /* 题号与判题结果共用一行，把底部空间留给更大的选项。 */
    s_lbl_quiz_progress = make_label(s_quiz_body, "", &ecard_font_12, C_MUTED, 12, 8, 120,
                                     LV_TEXT_ALIGN_LEFT);
    s_lbl_quiz_feedback = make_label(s_quiz_body, "", &ecard_font_12, C_MUTED, 92, 8, 104,
                                     LV_TEXT_ALIGN_RIGHT);
    lv_obj_t *bar_bg = make_box(s_quiz_body, 12, 26, 184, 4, C_LINE, NO_BORDER, 2);
    s_quiz_bar = make_box(bar_bg, 0, 0, 184, 4, C_BLUE, NO_BORDER, 2);

    s_lbl_quiz_prompt = make_wrapped_label(s_quiz_body, "", &ecard_font_16, C_INK,
                                           12, 38, QUIZ_PROMPT_W, QUIZ_PROMPT_MAX_H,
                                           LV_TEXT_ALIGN_LEFT);

    static const char *letters[ECARD_QUIZ_OPTION_COUNT] = {"A", "B", "C"};
    for (int i = 0; i < ECARD_QUIZ_OPTION_COUNT; ++i) {
        lv_obj_t *row = make_box(s_quiz_body, 12, 110 + i * 38, 184, 34, C_SURFACE, C_LINE, 10);
        lv_obj_t *badge = make_box(row, 8, 7, 20, 20, C_BLUE_SOFT, NO_BORDER, 10);
        lv_obj_t *letter = make_label(badge, letters[i], &ecard_font_12, C_BLUE, 0, 3, 20,
                                      LV_TEXT_ALIGN_CENTER);
        lv_obj_t *text = make_wrapped_label(row, "", &ecard_font_16, C_INK, 32, 6,
                                            QUIZ_OPTION_TEXT_W, 24, LV_TEXT_ALIGN_LEFT);
        s_quiz_options[i] = row;
        s_quiz_badges[i] = badge;
        s_lbl_quiz_letters[i] = letter;
        s_lbl_quiz_options[i] = text;
    }

    /* 结算面板：独立于答题主体，练完后整体切换。 */
    s_quiz_result = make_box(card, 0, 0, 208, 216, C_SURFACE, NO_BORDER, 0);
    lv_obj_set_style_bg_opa(s_quiz_result, LV_OPA_TRANSP, 0);
    make_label(s_quiz_result, "练习完成", &ecard_font_22, C_INK, 0, 32, 208, LV_TEXT_ALIGN_CENTER);
    s_lbl_quiz_score = make_label(s_quiz_result, "", &ecard_font_48, C_NAVY, 0, 72, 208,
                                  LV_TEXT_ALIGN_CENTER);
    s_lbl_quiz_caption = make_label(s_quiz_result, "", &ecard_font_16, C_MUTED, 0, 136, 208,
                                    LV_TEXT_ALIGN_CENTER);
    s_lbl_quiz_stars = make_label(s_quiz_result, "", &ecard_font_22, C_AMBER, 0, 164, 208,
                                  LV_TEXT_ALIGN_CENTER);
    make_label(s_quiz_result, "按 OK 返回菜单", &ecard_font_12, C_FAINT, 0, 198, 208,
               LV_TEXT_ALIGN_CENTER);

    create_page_hint(s_screen, "UP/DOWN 选择 · OK 提交 · 长按退出");
}

static void update_quiz_content(void) {
    if (!s_quiz_body || !s_quiz_result) return;

    if (ecard_quiz_is_finished()) {
        lv_obj_add_flag(s_quiz_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_quiz_result, LV_OBJ_FLAG_HIDDEN);

        char buf[32];
        snprintf(buf, sizeof(buf), "%u / %u", (unsigned)ecard_quiz_score(),
                 (unsigned)ecard_quiz_total());
        label_set_text(s_lbl_quiz_score, buf);
        snprintf(buf, sizeof(buf), "答对 %u 题", (unsigned)ecard_quiz_score());
        label_set_text(s_lbl_quiz_caption, buf);
        const char *stars = ecard_quiz_score() >= 9 ? "★★★"
                           : ecard_quiz_score() >= 7 ? "★★"
                           : ecard_quiz_score() >= 5 ? "★" : "继续加油";
        label_set_text(s_lbl_quiz_stars, stars);
        if (s_lbl_hint) {
            label_set_text(s_lbl_hint, "按 OK 返回菜单");
        }
        return;
    }

    lv_obj_clear_flag(s_quiz_body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_quiz_result, LV_OBJ_FLAG_HIDDEN);
    if (s_lbl_hint) {
        label_set_text(s_lbl_hint, "UP/DOWN 选择 · OK 提交 · 长按退出");
    }

    const ecard_quiz_question_t *q = ecard_quiz_current();
    if (!q) return;

    char buf[48];
    snprintf(buf, sizeof(buf), "第 %u 题 / 共 %u 题", (unsigned)(ecard_quiz_index() + 1),
             (unsigned)ecard_quiz_total());
    label_set_text(s_lbl_quiz_progress, buf);
    lv_obj_set_width(s_quiz_bar, (184 * (ecard_quiz_index() + 1)) / ecard_quiz_total());

    /* 题面：3 行 16 px 放不下时自动降到 12 px，最多占 4 行。 */
    int32_t prompt_h = 0;
    const lv_font_t *prompt_font = quiz_fit_font(q->prompt, QUIZ_PROMPT_W,
                                                 QUIZ_PROMPT_MAX_H, &prompt_h);
    if (prompt_h > QUIZ_PROMPT_MAX_H) prompt_h = QUIZ_PROMPT_MAX_H;
    lv_obj_set_style_text_font(s_lbl_quiz_prompt, prompt_font, 0);
    label_set_text(s_lbl_quiz_prompt, q->prompt);
    lv_obj_set_pos(s_lbl_quiz_prompt, 12, 38);
    lv_obj_set_size(s_lbl_quiz_prompt, QUIZ_PROMPT_W, prompt_h);

    /* 选项区高度随题面收缩；短题给到 44 px 行高，长题最低 30 px。 */
    const int32_t opt_start = 38 + prompt_h + 8;
    int32_t gap = 4;
    int32_t row_h = (QUIZ_OPTIONS_BOTTOM - opt_start - 2 * gap) / ECARD_QUIZ_OPTION_COUNT;
    if (row_h > 44) row_h = 44;
    if (row_h < 30) row_h = 30;
    if (row_h == 44) {
        gap = (QUIZ_OPTIONS_BOTTOM - opt_start - ECARD_QUIZ_OPTION_COUNT * row_h) / 2;
        if (gap < 4) gap = 4;
    }

    static const char *letters[ECARD_QUIZ_OPTION_COUNT] = {"A", "B", "C"};
    const bool answered = ecard_quiz_is_answered();
    const uint8_t selected = ecard_quiz_selected();

    for (int i = 0; i < ECARD_QUIZ_OPTION_COUNT; ++i) {
        const int32_t y = opt_start + i * (row_h + gap);
        lv_obj_set_pos(s_quiz_options[i], 12, y);
        lv_obj_set_size(s_quiz_options[i], 184, row_h);
        lv_obj_set_pos(s_quiz_badges[i], 8, (row_h - 20) / 2);

        int32_t opt_h = 0;
        const lv_font_t *opt_font = quiz_fit_font(q->options[i], QUIZ_OPTION_TEXT_W,
                                                  row_h - 4, &opt_h);
        if (opt_h > row_h - 4) opt_h = row_h - 4;
        lv_obj_set_style_text_font(s_lbl_quiz_options[i], opt_font, 0);
        label_set_text(s_lbl_quiz_options[i], q->options[i]);
        const int32_t oy = (opt_h <= row_h - 4) ? (row_h - opt_h) / 2 : 2;
        lv_obj_set_pos(s_lbl_quiz_options[i], 32, oy);
        lv_obj_set_size(s_lbl_quiz_options[i], QUIZ_OPTION_TEXT_W, opt_h);

        uint32_t border = C_LINE;
        uint32_t bg = C_SURFACE;
        uint32_t badge_bg = C_BLUE_SOFT;
        uint32_t letter_color = C_BLUE;
        int border_width = 1;

        if (answered && i == q->correct_index) {
            border = C_GREEN;
            bg = 0xECFDF5;
            badge_bg = C_GREEN;
            letter_color = C_SURFACE;
            border_width = 2;
        } else if (answered && i == selected) {
            border = C_RED;
            bg = C_RED_SOFT;
            badge_bg = C_RED;
            letter_color = C_SURFACE;
            border_width = 2;
        } else if (!answered && i == selected) {
            border = C_BLUE;
            bg = C_BLUE_SOFT;
            badge_bg = C_BLUE;
            letter_color = C_SURFACE;
            border_width = 2;
        }

        lv_obj_set_style_border_width(s_quiz_options[i], border_width, 0);
        lv_obj_set_style_border_color(s_quiz_options[i], lv_color_hex(border), 0);
        lv_obj_set_style_bg_color(s_quiz_options[i], lv_color_hex(bg), 0);
        lv_obj_set_style_bg_color(s_quiz_badges[i], lv_color_hex(badge_bg), 0);
        lv_obj_set_style_text_color(s_lbl_quiz_letters[i], lv_color_hex(letter_color), 0);
        lv_obj_set_style_text_color(s_lbl_quiz_options[i], lv_color_hex(C_INK), 0);
    }

    if (!answered) {
        label_set_text(s_lbl_quiz_feedback, "");
    } else if (selected == q->correct_index) {
        label_set_text(s_lbl_quiz_feedback, "✓ 正确");
        lv_obj_set_style_text_color(s_lbl_quiz_feedback, lv_color_hex(C_GREEN), 0);
    } else {
        snprintf(buf, sizeof(buf), "× 答案 %s", letters[q->correct_index]);
        label_set_text(s_lbl_quiz_feedback, buf);
        lv_obj_set_style_text_color(s_lbl_quiz_feedback, lv_color_hex(C_RED), 0);
    }
    if (!answered) {
        lv_obj_set_style_text_color(s_lbl_quiz_feedback, lv_color_hex(C_MUTED), 0);
    }
}

/* ------------------------------------------------------------------------- */
/* 番茄钟                                                                    */
/* ------------------------------------------------------------------------- */

static void update_pomodoro_content(void);

static void create_pomodoro_page(void) {
    s_screen = create_page_root();
    create_page_header(s_screen, "番茄钟", "选时长 · 最长 50 分钟");

    lv_obj_t *card = make_box(s_screen, 16, 66, 208, 224, C_SURFACE, C_LINE, 18);

    s_pomo_phase = make_label(card, "", &ecard_font_16, C_BLUE, 0, 16, 208,
                              LV_TEXT_ALIGN_CENTER);
    s_pomo_time = make_label(card, "25:00", &ecard_font_48, C_NAVY, 0, 44, 208,
                             LV_TEXT_ALIGN_CENTER);
    lv_obj_t *bar_bg = make_box(card, 12, 108, 184, 6, C_LINE, NO_BORDER, 3);
    s_pomo_bar = make_box(bar_bg, 0, 0, 0, 6, C_BLUE, NO_BORDER, 3);
    s_pomo_round = make_label(card, "", &ecard_font_12, C_MUTED, 0, 124, 208,
                              LV_TEXT_ALIGN_CENTER);
    s_pomo_tip = make_wrapped_label(card, "", &ecard_font_16, C_INK, 16, 158, 176, 52,
                                    LV_TEXT_ALIGN_CENTER);

    create_page_hint(s_screen, "OK 开始/暂停 · DOWN 结束 · 长按退出");
    update_pomodoro_content();
}

static const char *pomodoro_phase_text(ecard_pomodoro_phase_t phase) {
    switch (phase) {
    case ECARD_POMODORO_FOCUS:
        return "专注中";
    case ECARD_POMODORO_BREAK:
        return "休息一下";
    case ECARD_POMODORO_LONG_BREAK:
        return "长休息";
    case ECARD_POMODORO_DONE:
        return "休息结束";
    default:
        return "准备开始";
    }
}

static void update_pomodoro_content(void) {
    if (!s_pomo_time) return;

    const ecard_pomodoro_phase_t phase = ecard_pomodoro_phase();
    const uint32_t total = ecard_pomodoro_phase_total_ms();
    uint32_t remaining = ecard_pomodoro_remaining_ms();
    if (remaining == 0 && total > 0) remaining = total;

    char buf[32];
    const uint32_t sec = (phase == ECARD_POMODORO_IDLE)
        ? (uint32_t)ecard_pomodoro_focus_minutes() * 60u
        : ecard_pomodoro_remaining_sec();
    snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(sec / 60u), (unsigned)(sec % 60u));
    label_set_text(s_pomo_time, buf);
    label_set_text(s_pomo_phase, pomodoro_phase_text(phase));

    int32_t done_w = 0;
    if (total > 0) {
        done_w = (int32_t)((uint64_t)184 * (total - remaining) / total);
    }
    lv_obj_set_width(s_pomo_bar, done_w);

    char round[40];
    snprintf(round, sizeof(round), "已完成 %u 轮 · 第 %u 轮",
             (unsigned)ecard_pomodoro_completed(),
             (unsigned)(ecard_pomodoro_completed() + 1));
    label_set_text(s_pomo_round, round);

    char idle_tip[72];
    snprintf(idle_tip, sizeof(idle_tip), "UP +5 分 · DOWN -5 分\nOK 开始 %u 分钟",
             (unsigned)ecard_pomodoro_focus_minutes());
    const char *tip = idle_tip;
    if (phase == ECARD_POMODORO_FOCUS) {
        tip = ecard_pomodoro_is_running() ? "专心做题吧\nOK 暂停 · DOWN 结束"
                                          : "已暂停\n按 OK 继续";
    } else if (phase == ECARD_POMODORO_BREAK || phase == ECARD_POMODORO_LONG_BREAK) {
        tip = "起来活动一下";
    } else if (phase == ECARD_POMODORO_DONE) {
        tip = idle_tip;
    }
    label_set_text(s_pomo_tip, tip);

    const uint32_t colors[5] = {C_MUTED, C_BLUE, C_GREEN, C_AMBER, C_NAVY};
    lv_obj_set_style_text_color(s_pomo_phase, lv_color_hex(colors[phase]), 0);
}

/* ------------------------------------------------------------------------- */
/* 年级选择                                                                  */
/* ------------------------------------------------------------------------- */

static const char *grade_name(uint8_t grade) {
    static const char *names[ECARD_GRADE_COUNT] = {"一", "二", "三", "四", "五", "六"};
    if (grade < 1 || grade > ECARD_GRADE_COUNT) return names[0];
    return names[grade - 1];
}

static void apply_grade_selection(void) {
    for (int i = 0; i < ECARD_GRADE_COUNT; ++i) {
        lv_obj_t *row = s_grade_rows[i];
        if (!row) continue;
        bool selected = (i == (int)s_grade_selection);

        lv_obj_set_style_border_width(row, selected ? 0 : 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(C_LINE), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(selected ? C_NAVY : C_SURFACE), 0);
        lv_obj_set_style_bg_grad_color(row, lv_color_hex(C_NAVY_HI), 0);
        lv_obj_set_style_bg_grad_dir(row, selected ? LV_GRAD_DIR_VER : LV_GRAD_DIR_NONE, 0);
        if (s_grade_tiles[i]) {
            lv_obj_set_style_bg_color(s_grade_tiles[i],
                                      lv_color_hex(selected ? 0x2A517C : C_BLUE_SOFT), 0);
        }
        if (s_grade_glyphs[i]) {
            lv_obj_set_style_text_color(s_grade_glyphs[i],
                                        lv_color_hex(selected ? C_SURFACE : C_BLUE), 0);
        }
        if (s_grade_titles[i]) {
            lv_obj_set_style_text_color(s_grade_titles[i],
                                        lv_color_hex(selected ? C_SURFACE : C_INK), 0);
        }
        if (s_grade_subs[i]) {
            lv_obj_set_style_text_color(s_grade_subs[i],
                                        lv_color_hex(selected ? C_ON_NAVY : C_MUTED), 0);
        }
    }
}

static void create_pick_page(ecard_card_kind_t kind) {
    const bool poem = (kind == ECARD_CARD_POEM);
    s_screen = create_page_root();
    create_page_header(s_screen, poem ? "古诗接句" : "英语单词", "选择年级");

    for (int i = 0; i < ECARD_GRADE_COUNT; ++i) {
        const uint8_t grade = (uint8_t)(i + 1);
        uint8_t count = 0;
        if (poem) {
            (void)ecard_cards_poems(grade, &count);
        } else {
            (void)ecard_cards_words(grade, &count);
        }

        int y = 62 + i * 40;
        lv_obj_t *row = make_box(s_screen, 16, y, 208, 36, C_SURFACE, C_LINE, 11);
        lv_obj_t *tile = make_box(row, 7, 4, 28, 28, C_BLUE_SOFT, NO_BORDER, 8);
        static const char *digits[ECARD_GRADE_COUNT] = {"1", "2", "3", "4", "5", "6"};
        lv_obj_t *glyph = make_label(tile, digits[i], &ecard_font_16, C_BLUE, 0, 5, 28,
                                     LV_TEXT_ALIGN_CENTER);
        char title[24];
        snprintf(title, sizeof(title), "%s年级", grade_name(grade));
        lv_obj_t *lbl_title = make_label(row, title, &ecard_font_16, C_INK, 42, 1, 110,
                                         LV_TEXT_ALIGN_LEFT);
        char sub[32];
        snprintf(sub, sizeof(sub), "%u 张 · 按 OK 开始", (unsigned)count);
        lv_obj_t *lbl_sub = make_label(row, sub, &ecard_font_12, C_MUTED, 42, 20, 148,
                                       LV_TEXT_ALIGN_LEFT);
        make_label(row, "›", &ecard_font_16, C_FAINT, 186, 9, 16, LV_TEXT_ALIGN_CENTER);

        s_grade_rows[i] = row;
        s_grade_tiles[i] = tile;
        s_grade_glyphs[i] = glyph;
        s_grade_titles[i] = lbl_title;
        s_grade_subs[i] = lbl_sub;
    }

    create_page_hint(s_screen, "UP / DOWN 选年级 · OK 开始");
    apply_grade_selection();
}

/* ------------------------------------------------------------------------- */
/* 古诗接句 / 英语单词闪卡                                                    */
/* ------------------------------------------------------------------------- */

#define FC_CARD_Y   62
#define FC_CARD_H   232
#define FC_TEXT_X   12
#define FC_TEXT_W   184
#define FC_FACE_Y   28
#define FC_FACE_H   190
/* 底部一行出处/分类占 26 px；题面只在它上方的区域垂直居中。 */
#define FC_META_Y   192
#define FC_META_H   22

/* 中文卡面只用 16/22 两档；英语单词允许上探 48，视觉更接近实体卡。 */
static const lv_font_t *fc_fit_font(const char *text, int32_t width, int32_t max_h,
                                    bool allow_large, int32_t *out_h) {
    if (allow_large) {
        int32_t large_h = quiz_text_height(text, &ecard_font_48, width);
        if (large_h <= max_h && large_h <= 48) {
            if (out_h) *out_h = large_h;
            return &ecard_font_48;
        }
    }
    const lv_font_t *font = &ecard_font_22;
    int32_t h = quiz_text_height(text, font, width);
    if (h > max_h) {
        font = &ecard_font_16;
        h = quiz_text_height(text, font, width);
    }
    if (h > max_h) {
        font = &ecard_font_12;
        h = quiz_text_height(text, font, width);
    }
    if (out_h) *out_h = h;
    return font;
}

/* 标题与说明合并后内容整体上移，闪卡页固定使用 FC_CARD_Y。 */
static void create_flashcard_page(ecard_card_kind_t kind) {
    const bool poem = (kind == ECARD_CARD_POEM);
    s_screen = create_page_root();
    char subtitle[32];
    snprintf(subtitle, sizeof(subtitle), "%s年级 · %s", grade_name(s_deck_grade),
             poem ? "翻面读下句" : "翻面看释义");
    create_page_header(s_screen, poem ? "古诗接句" : "英语单词", subtitle);

    lv_obj_t *card = make_box(s_screen, 16, FC_CARD_Y, 208, FC_CARD_H,
                              C_SURFACE, C_LINE, 18);

    s_fc_body = make_box(card, 0, 0, 208, FC_CARD_H, C_SURFACE, NO_BORDER, 0);
    lv_obj_set_style_bg_opa(s_fc_body, LV_OPA_TRANSP, 0);

    s_lbl_fc_progress = make_label(s_fc_body, "", &ecard_font_12, C_MUTED, 12, 6, 120,
                                   LV_TEXT_ALIGN_LEFT);
    s_fc_kind = make_box(s_fc_body, 130, 4, 58, 20, C_BLUE_SOFT, NO_BORDER, 10);
    s_lbl_fc_kind = make_label(s_fc_kind, poem ? "古诗" : "单词", &ecard_font_12, C_BLUE,
                               0, 3, 58, LV_TEXT_ALIGN_CENTER);

    /* 正面：整块浅色卡面居中显示题面，底部一行小字给出出处或音标。 */
    s_fc_face = make_box(s_fc_body, FC_TEXT_X, FC_FACE_Y, FC_TEXT_W, FC_FACE_H,
                         C_BLUE_SOFT, NO_BORDER, 14);
    /* 题面与出处挂在卡片主体上，翻面时只需隐藏浅色卡面底图。 */
    s_lbl_fc_front = make_wrapped_label(s_fc_body, "", &ecard_font_22, C_INK,
                                        FC_TEXT_X, 28, FC_TEXT_W, 96,
                                        LV_TEXT_ALIGN_CENTER);
    s_lbl_fc_source = make_label(s_fc_body, "", &ecard_font_12, C_FAINT, FC_TEXT_X, FC_META_Y,
                                 FC_TEXT_W, LV_TEXT_ALIGN_CENTER);

    /* 背面：上句留在顶部做对照，下句放大；其余纵向空间给释义与例句。 */
    s_fc_divider = make_box(s_fc_body, 46, 110, 116, 1, C_LINE, NO_BORDER, 0);
    s_lbl_fc_back = make_wrapped_label(s_fc_body, "", &ecard_font_22, C_BLUE,
                                       FC_TEXT_X, 52, FC_TEXT_W, 52, LV_TEXT_ALIGN_CENTER);
    s_lbl_fc_explain = make_wrapped_label(s_fc_body, "", &ecard_font_16, C_MUTED,
                                          FC_TEXT_X, 114, FC_TEXT_W, 40, LV_TEXT_ALIGN_LEFT);
    s_lbl_fc_example = make_wrapped_label(s_fc_body, "", &ecard_font_16, C_INK,
                                          FC_TEXT_X, 160, FC_TEXT_W, 40, LV_TEXT_ALIGN_LEFT);
    s_lbl_fc_example_cn = make_wrapped_label(s_fc_body, "", &ecard_font_12, C_FAINT,
                                             FC_TEXT_X, 202, FC_TEXT_W, 18, LV_TEXT_ALIGN_LEFT);

    /* 结算面板：和答题页同一套版式，降低认知切换成本。 */
    s_fc_result = make_box(card, 0, 0, 208, FC_CARD_H, C_SURFACE, NO_BORDER, 0);
    lv_obj_set_style_bg_opa(s_fc_result, LV_OPA_TRANSP, 0);
    make_label(s_fc_result, "本轮完成", &ecard_font_22, C_INK, 0, 36, 208, LV_TEXT_ALIGN_CENTER);
    s_lbl_fc_score = make_label(s_fc_result, "", &ecard_font_48, C_NAVY, 0, 76, 208,
                                LV_TEXT_ALIGN_CENTER);
    s_lbl_fc_caption = make_label(s_fc_result, "", &ecard_font_16, C_MUTED, 0, 136, 208,
                                  LV_TEXT_ALIGN_CENTER);
    make_label(s_fc_result, "★★★", &ecard_font_22, C_AMBER, 0, 166, 208, LV_TEXT_ALIGN_CENTER);

    create_page_hint(s_screen, "OK 翻面 · 长按退出");
}

static void update_flashcard_content(void) {
    if (!s_fc_body || !s_fc_result) return;

    if (ecard_flashcard_is_finished()) {
        lv_obj_add_flag(s_fc_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_fc_result, LV_OBJ_FLAG_HIDDEN);

        char buf[32];
        snprintf(buf, sizeof(buf), "%u / %u", (unsigned)ecard_flashcard_known(),
                 (unsigned)ecard_flashcard_total());
        label_set_text(s_lbl_fc_score, buf);
        label_set_text(s_lbl_fc_caption, "全部掌握，真棒！");
        if (s_lbl_hint) label_set_text(s_lbl_hint, "按 OK 返回菜单");
        return;
    }

    lv_obj_clear_flag(s_fc_body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_fc_result, LV_OBJ_FLAG_HIDDEN);

    const ecard_card_t *c = ecard_flashcard_current();
    if (!c) return;

    char buf[48];
    snprintf(buf, sizeof(buf), "第 %u 张 · 剩 %u", (unsigned)(ecard_flashcard_known() + 1),
             (unsigned)ecard_flashcard_remaining());
    label_set_text(s_lbl_fc_progress, buf);

    const bool poem = (s_page == ECARD_UI_PAGE_POEM);
    const bool flipped = ecard_flashcard_is_flipped();
    label_set_text(s_lbl_fc_kind, poem ? "古诗" : "单词");
    lv_obj_set_style_bg_color(s_fc_kind, lv_color_hex(poem ? C_AMBER_BG : C_BLUE_SOFT), 0);
    lv_obj_set_style_text_color(s_lbl_fc_kind, lv_color_hex(poem ? 0x92400E : C_BLUE), 0);

    if (!flipped) {
        lv_obj_clear_flag(s_fc_face, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_fc_divider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_lbl_fc_back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_lbl_fc_explain, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_lbl_fc_example, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_lbl_fc_example_cn, LV_OBJ_FLAG_HIDDEN);

        int32_t h = 0;
        const lv_font_t *font = fc_fit_font(c->front, FC_TEXT_W,
                                            FC_META_Y - 8 - FC_FACE_Y, !poem, &h);
        lv_obj_set_style_text_font(s_lbl_fc_front, font, 0);
        label_set_text(s_lbl_fc_front, c->front);
        /* 只在出处/分类上方那块区域居中：底部小字不参与题面居中计算。 */
        const int32_t area_top = FC_FACE_Y;
        const int32_t area_h = FC_META_Y - 8 - FC_FACE_Y;
        lv_obj_set_pos(s_lbl_fc_front, FC_TEXT_X, area_top + (area_h - h) / 2);
        lv_obj_set_size(s_lbl_fc_front, FC_TEXT_W, h);
        lv_obj_clear_flag(s_lbl_fc_source, LV_OBJ_FLAG_HIDDEN);
        label_set_text(s_lbl_fc_source, c->meta);
        if (s_lbl_hint) label_set_text(s_lbl_hint, "OK 翻面 · 长按退出");
        return;
    }

    /* 翻面后不再出现第二套按键图例：底部提示已说明 UP/DOWN，腾出的空间给释义。 */
    lv_obj_add_flag(s_fc_face, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_fc_source, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_fc_divider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_lbl_fc_back, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_lbl_fc_explain, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_style_text_font(s_lbl_fc_front, &ecard_font_16, 0);
    label_set_text(s_lbl_fc_front, c->front);
    lv_obj_set_pos(s_lbl_fc_front, FC_TEXT_X, 30);
    lv_obj_set_size(s_lbl_fc_front, FC_TEXT_W, 22);

    int32_t back_h = 0;
    /* 背面中文只到 22 px：48 px 字库只保留 ASCII，省下大量中文字形。 */
    const lv_font_t *back_font = fc_fit_font(c->back, FC_TEXT_W, 50, false, &back_h);
    lv_obj_set_style_text_font(s_lbl_fc_back, back_font, 0);
    label_set_text(s_lbl_fc_back, c->back);
    lv_obj_set_pos(s_lbl_fc_back, FC_TEXT_X, 56 + (50 - back_h) / 2);
    lv_obj_set_size(s_lbl_fc_back, FC_TEXT_W, back_h);

    if (poem) {
        /* 古诗只有释义，可以用更大的字并占满下半区。 */
        int32_t ex_h = 0;
        const lv_font_t *ex_font = fc_fit_font(c->explain, FC_TEXT_W, 92, false, &ex_h);
        lv_obj_set_style_text_font(s_lbl_fc_explain, ex_font, 0);
        label_set_text(s_lbl_fc_explain, c->explain);
        lv_obj_set_pos(s_lbl_fc_explain, FC_TEXT_X, 118);
        lv_obj_set_size(s_lbl_fc_explain, FC_TEXT_W, ex_h);
        lv_obj_add_flag(s_lbl_fc_example, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_lbl_fc_example_cn, LV_OBJ_FLAG_HIDDEN);
        if (s_lbl_hint) label_set_text(s_lbl_hint, "UP 不认识 · DOWN 认识 · 长按退出");
        return;
    }

    /* 单词：中文词义已经足够直白，不再解释“铅笔是什么”，把空间留给例句。 */
    lv_obj_add_flag(s_lbl_fc_explain, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_lbl_fc_example, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_lbl_fc_example_cn, LV_OBJ_FLAG_HIDDEN);
    label_set_text(s_lbl_fc_example, c->example ? c->example : "");
    label_set_text(s_lbl_fc_example_cn, c->example_cn ? c->example_cn : "");

    /* 例句与翻译作为一个整体，在分隔线以下的区域垂直居中。 */
    int32_t ex_h = quiz_text_height(c->example ? c->example : "", &ecard_font_16,
                                    FC_TEXT_W);
    if (ex_h < 22) ex_h = 22;
    if (ex_h > 66) ex_h = 66;
    const int32_t block_h = ex_h + 8 + 20;
    const int32_t area_top = 118;
    const int32_t area_bottom = FC_CARD_H - 10;
    int32_t y = area_top + ((area_bottom - area_top) - block_h) / 2;
    if (y < area_top) y = area_top;

    lv_obj_set_size(s_lbl_fc_example, FC_TEXT_W, ex_h);
    lv_obj_set_pos(s_lbl_fc_example, FC_TEXT_X, y);
    lv_obj_set_size(s_lbl_fc_example_cn, FC_TEXT_W, 20);
    lv_obj_set_pos(s_lbl_fc_example_cn, FC_TEXT_X, y + ex_h + 8);
    if (s_lbl_hint) label_set_text(s_lbl_hint, "UP 不认识 · DOWN 认识 · 长按退出");
}

/* ------------------------------------------------------------------------- */
/* 语音留言                                                                  */
/* ------------------------------------------------------------------------- */

static void create_messages_page(void) {
    s_screen = create_page_root();
    create_page_header(s_screen, "语音留言", "近 24 小时 · 收/发");

    lv_obj_t *card = make_box(s_screen, 16, 70, 208, 218, C_SURFACE, C_LINE, 18);

    /* 滚动列表：一行一条，最多同时露出 4 条，选中行始终滚动到可视区。 */
    s_msg_list = lv_obj_create(card);
    lv_obj_set_pos(s_msg_list, 4, 4);
    lv_obj_set_size(s_msg_list, 200, 210);
    lv_obj_set_style_bg_opa(s_msg_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_msg_list, 0, 0);
    lv_obj_set_style_pad_all(s_msg_list, 0, 0);
    lv_obj_set_style_pad_row(s_msg_list, 6, 0);
    lv_obj_set_flex_flow(s_msg_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(s_msg_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_msg_list, LV_OBJ_FLAG_CLICKABLE);

    s_msg_empty_panel = make_box(card, 0, 0, 208, 216, C_SURFACE, NO_BORDER, 0);
    lv_obj_t *empty_icon = make_box(s_msg_empty_panel, 72, 44, 64, 64, C_BLUE_SOFT, NO_BORDER, 32);
    make_label(empty_icon, "信", &ecard_font_22, C_BLUE, 0, 19, 64, LV_TEXT_ALIGN_CENTER);
    make_label(s_msg_empty_panel, "暂无留言", &ecard_font_16, C_INK, 24, 128, 160,
               LV_TEXT_ALIGN_CENTER);
    make_label(s_msg_empty_panel, "收发的留言会显示在这里", &ecard_font_12, C_FAINT,
               24, 156, 160, LV_TEXT_ALIGN_CENTER);

    create_page_hint(s_screen, "UP/DOWN 选择 · OK 播放 · 长按返回");
}

/* ------------------------------------------------------------------------- */
/* 首次配网引导 / 设备重置                                                   */
/* ------------------------------------------------------------------------- */

static int setup_stage(void) {
    if (ecard_setup_is_provisioned()) return 3;
    if (!ecard_setup_has_wifi()) {
        return ecard_portal_is_active() ? 0 : -1;
    }
    if (!ecard_netlink_is_connected()) return 1;
    return 2;
}

static void create_setup_page(void) {
    const int stage = setup_stage();
    s_setup_stage = stage;

    const char *ap_ssid = ecard_portal_ssid();
    if (!ap_ssid || !ap_ssid[0]) ap_ssid = "正在启动热点…";

    s_screen = create_page_root();
    if (stage == 2) {
        create_page_header(s_screen, "填写身份", "第 2 步 · 共 2 步");
    } else if (stage == 3) {
        create_page_header(s_screen, "欢迎使用", "配置完成");
    } else {
        create_page_header(s_screen, "欢迎使用", "手机配网 · 两步完成");
    }

    lv_obj_t *card = make_box(s_screen, 16, 66, 208, 224, C_SURFACE, C_LINE, 18);

    if (stage == 2) {
        const ecard_setup_t *cfg = ecard_setup_get();
        const char *wifi = ecard_netlink_is_connected() ? ecard_netlink_ssid()
                                                        : cfg->wifi_ssid;

        make_label(card, "Wi-Fi 已连接", &ecard_font_16, C_GREEN, 14, 18, 180,
                   LV_TEXT_ALIGN_LEFT);
        make_label(card, wifi, &ecard_font_12, C_BLUE, 14, 44, 180, LV_TEXT_ALIGN_LEFT);
        make_box(card, 14, 68, 180, 1, C_LINE, NO_BORDER, 0);

        make_label(card, "请在手机上填写身份信息", &ecard_font_16, C_INK, 14, 80, 180,
                   LV_TEXT_ALIGN_LEFT);
        make_label(card, "浏览器会自动跳转", &ecard_font_12, C_MUTED, 14, 104, 180,
                   LV_TEXT_ALIGN_LEFT);
        make_label(card, "或打开 192.168.4.1", &ecard_font_12, C_BLUE, 14, 122, 180,
                   LV_TEXT_ALIGN_LEFT);
        make_box(card, 14, 146, 180, 1, C_LINE, NO_BORDER, 0);

        make_label(card, "热点", &ecard_font_12, C_FAINT, 14, 158, 40,
                   LV_TEXT_ALIGN_LEFT);
        make_label(card, ap_ssid, &ecard_font_12, C_INK, 54, 158, 140,
                   LV_TEXT_ALIGN_LEFT);
        make_label(card, "密码 passport1234", &ecard_font_12, C_FAINT, 14, 178, 180,
                   LV_TEXT_ALIGN_LEFT);
        create_page_hint(s_screen, "填写后自动保存并重启");
        return;
    }

    if (stage == 3) {
        make_label(card, "配置已完成", &ecard_font_16, C_GREEN, 0, 72, 208,
                   LV_TEXT_ALIGN_CENTER);
        make_label(card, "设备正在重启…", &ecard_font_12, C_MUTED, 0, 102, 208,
                   LV_TEXT_ALIGN_CENTER);
        create_page_hint(s_screen, "请稍候");
        return;
    }

    if (stage < 0) {
        make_label(card, "热点启动失败", &ecard_font_16, C_RED, 0, 62, 208,
                   LV_TEXT_ALIGN_CENTER);
        make_label(card, "长按 OK 重新启动", &ecard_font_12, C_MUTED, 0, 94, 208,
                   LV_TEXT_ALIGN_CENTER);
        create_page_hint(s_screen, "长按 OK 重试");
        return;
    }

    if (stage == 1) {
        const ecard_setup_t *cfg = ecard_setup_get();
        const bool failed = ecard_netlink_fail_count() > 0;

        make_label(card, "正在连接 Wi-Fi", &ecard_font_16, C_INK, 14, 18, 180,
                   LV_TEXT_ALIGN_LEFT);
        make_label(card, cfg->wifi_ssid, &ecard_font_12, C_BLUE, 14, 44, 180,
                   LV_TEXT_ALIGN_LEFT);
        make_label(card, failed ? "连接失败，请检查密码" : "密码已保存，正在连接",
                   &ecard_font_12, failed ? C_RED : C_MUTED, 14, 62, 180,
                   LV_TEXT_ALIGN_LEFT);
        make_box(card, 14, 88, 180, 1, C_LINE, NO_BORDER, 0);

        make_label(card, "如果连接失败", &ecard_font_16, C_INK, 14, 98, 180,
                   LV_TEXT_ALIGN_LEFT);
        make_label(card, "长按 OK 立即重试", &ecard_font_12, C_MUTED, 14, 120, 180,
                   LV_TEXT_ALIGN_LEFT);
        make_label(card, "每 2 分钟自动重试一次", &ecard_font_12, C_FAINT, 14, 138, 180,
                   LV_TEXT_ALIGN_LEFT);
        make_box(card, 14, 160, 180, 1, C_LINE, NO_BORDER, 0);

        make_label(card, "热点", &ecard_font_12, C_FAINT, 14, 170, 40,
                   LV_TEXT_ALIGN_LEFT);
        make_label(card, ap_ssid, &ecard_font_12, C_INK, 54, 170, 140,
                   LV_TEXT_ALIGN_LEFT);
        make_label(card, "打开 192.168.4.1", &ecard_font_12, C_BLUE, 14, 190, 180,
                   LV_TEXT_ALIGN_LEFT);
        create_page_hint(s_screen, "长按 OK 重试 · 手机仍可修改 Wi-Fi");
        return;
    }

    /* 阶段 0：等待手机连接热点并填写 Wi-Fi。 */
    make_label(card, "连接热点", &ecard_font_16, C_INK, 14, 16, 180,
               LV_TEXT_ALIGN_LEFT);
    make_label(card, ap_ssid, &ecard_font_12, C_BLUE, 14, 40, 180, LV_TEXT_ALIGN_LEFT);
    make_label(card, "密码 passport1234", &ecard_font_12, C_MUTED, 14, 58, 180,
               LV_TEXT_ALIGN_LEFT);
    make_box(card, 14, 82, 180, 1, C_LINE, NO_BORDER, 0);

    make_label(card, "1  填写 Wi-Fi", &ecard_font_16, C_INK, 14, 92, 180,
               LV_TEXT_ALIGN_LEFT);
    make_label(card, "连接后自动打开配置页", &ecard_font_12, C_MUTED, 14, 116, 180,
               LV_TEXT_ALIGN_LEFT);
    make_label(card, "无需手动输入网址", &ecard_font_12, C_FAINT, 14, 134, 180,
               LV_TEXT_ALIGN_LEFT);
    make_box(card, 14, 158, 180, 1, C_LINE, NO_BORDER, 0);

    make_label(card, "2  填写身份", &ecard_font_16, C_INK, 14, 168, 180,
               LV_TEXT_ALIGN_LEFT);
    make_label(card, "连上 Wi-Fi 后自动进入", &ecard_font_12, C_MUTED, 14, 192, 180,
               LV_TEXT_ALIGN_LEFT);
    create_page_hint(s_screen, "手机连上热点后会自动弹出配置页");
}

static void create_reset_page(void) {
    s_screen = create_page_root();
    create_page_header(s_screen, "设备重置", "清除全部配置");

    lv_obj_t *card = make_box(s_screen, 16, 66, 208, 224, C_SURFACE, C_LINE, 18);
    lv_obj_t *icon = make_box(card, 84, 30, 40, 40, C_RED_SOFT, NO_BORDER, 20);
    make_label(icon, "!", &ecard_font_22, C_RED, 0, 8, 40, LV_TEXT_ALIGN_CENTER);

    make_label(card, "将删除以下内容", &ecard_font_16, C_INK, 0, 84, 208, LV_TEXT_ALIGN_CENTER);
    make_label(card, "Wi-Fi 配置 · 身份信息 · 家长绑定", &ecard_font_12, C_MUTED, 0, 108, 208,
               LV_TEXT_ALIGN_CENTER);
    make_label(card, "学习记录与闪卡进度不受影响", &ecard_font_12, C_FAINT, 0, 126, 208,
               LV_TEXT_ALIGN_CENTER);
    make_label(card, "按 OK 确认重置", &ecard_font_16, C_RED, 0, 160, 208, LV_TEXT_ALIGN_CENTER);

    create_page_hint(s_screen, "OK 确认 · 长按 OK 返回");
}

/* ------------------------------------------------------------------------- */
/* 设备信息                                                                  */
/* ------------------------------------------------------------------------- */

static void create_device_page(void) {
    s_screen = create_page_root();
    create_page_header(s_screen, "设备信息", "电量 · 存储");

    lv_obj_t *card = make_box(s_screen, 16, 72, 208, 214, C_SURFACE, C_LINE, 18);

    make_label(card, "电量", &ecard_font_12, C_FAINT, 14, 14, 60, LV_TEXT_ALIGN_LEFT);
    char bat[16];
    format_battery(bat, sizeof(bat));
    s_lbl_dev_battery = make_label(card, bat, &ecard_font_16, C_INK, 118, 10, 76,
                                   LV_TEXT_ALIGN_RIGHT);
    s_bar_dev_battery = lv_bar_create(card);
    lv_obj_set_pos(s_bar_dev_battery, 14, 40);
    lv_obj_set_size(s_bar_dev_battery, 180, 6);
    lv_obj_set_style_bg_color(s_bar_dev_battery, lv_color_hex(C_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar_dev_battery, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar_dev_battery, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_dev_battery, lv_color_hex(battery_color()), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_dev_battery, 3, LV_PART_INDICATOR);
    lv_bar_set_range(s_bar_dev_battery, 0, 100);
    lv_bar_set_value(s_bar_dev_battery, s_battery_pct < 0 ? 0 : s_battery_pct, LV_ANIM_OFF);

    make_box(card, 14, 58, 180, 1, C_LINE, NO_BORDER, 0);

    /* 四行键值：标签 12 px 下移 4 px 与 16 px 数值共享基线。 */
    make_label(card, "网络", &ecard_font_12, C_FAINT, 14, 70, 60, LV_TEXT_ALIGN_LEFT);
    s_lbl_dev_network = make_label(card, s_wifi_connected ? "校园网已连接" : "未连接",
                                   &ecard_font_16, C_INK, 94, 66, 100, LV_TEXT_ALIGN_RIGHT);

    make_label(card, "固件", &ecard_font_12, C_FAINT, 14, 94, 60, LV_TEXT_ALIGN_LEFT);
    make_label(card, "eCard 1.0.0", &ecard_font_16, C_INK, 94, 90, 100, LV_TEXT_ALIGN_RIGHT);

    make_label(card, "存储", &ecard_font_12, C_FAINT, 14, 118, 60, LV_TEXT_ALIGN_LEFT);
    make_label(card, "16 KB", &ecard_font_16, C_INK, 94, 114, 100, LV_TEXT_ALIGN_RIGHT);

    make_label(card, "模式", &ecard_font_12, C_FAINT, 14, 142, 60, LV_TEXT_ALIGN_LEFT);
    make_label(card, "电子学生证", &ecard_font_16, C_INK, 94, 138, 100, LV_TEXT_ALIGN_RIGHT);

    s_dev_note = make_box(card, 12, 164, 184, 38, C_RED_SOFT, 0xF6D5D5, 12);
    s_dev_note_badge = make_box(s_dev_note, 10, 9, 20, 20, C_RED, NO_BORDER, 10);
    make_label(s_dev_note_badge, "!", &ecard_font_12, C_SURFACE, 0, 2, 20,
               LV_TEXT_ALIGN_CENTER);
    s_lbl_dev_note_title = make_label(s_dev_note, "紧急求助", &ecard_font_16, C_RED, 40, 3,
                                      138, LV_TEXT_ALIGN_LEFT);
    s_lbl_dev_note_sub = make_label(s_dev_note, "长按 DOWN 键 3 秒", &ecard_font_12, C_RED,
                                    40, 22, 138, LV_TEXT_ALIGN_LEFT);

    create_page_hint(s_screen, "OK 返回 · DOWN 设备重置");
}

/* ------------------------------------------------------------------------- */
/* 模态层                                                                    */
/* ------------------------------------------------------------------------- */

static void reset_modal_pointers(void) {
    s_modal_container = NULL;
    s_lbl_modal_big = NULL;
    s_lbl_modal_kicker = NULL;
    s_lbl_modal_title = NULL;
    s_lbl_modal_sub = NULL;
    s_lbl_modal_hint = NULL;
    s_bar_modal = NULL;
    memset(s_wave_bars, 0, sizeof(s_wave_bars));
}

static lv_obj_t *modal_begin(uint32_t bg) {
    if (s_modal_container) {
        lv_obj_delete(s_modal_container);
        reset_modal_pointers();
    }
    s_modal_container = make_box(s_screen, 0, 0, 240, 320, bg, NO_BORDER, 0);
    lv_obj_move_foreground(s_modal_container);
    return s_modal_container;
}

static void modal_progress_bar(lv_obj_t *parent, int x, int y, int w, int value, int max) {
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, 6);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_NAVY_SOFT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, max);
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    s_bar_modal = bar;
}

static void create_waveform(lv_obj_t *parent, int base_y) {
    for (int i = 0; i < 11; ++i) {
        int x = 62 + i * 11;
        s_wave_bars[i] = make_box(parent, x, base_y - 12, 5, 12, C_BLUE, NO_BORDER, 2);
    }
}

static void update_waveform(uint8_t amplitude) {
    int base_y = 222;
    for (int i = 0; i < 11; ++i) {
        if (!s_wave_bars[i]) continue;
        int h = 8 + (int)(((uint32_t)amplitude * (uint32_t)s_wave_pattern[i]) / 100);
        if (h < 6) h = 6;
        if (h > 44) h = 44;
        lv_obj_set_height(s_wave_bars[i], h);
        lv_obj_set_y(s_wave_bars[i], base_y - h);
    }
}

static void create_recording_modal(void) {
    modal_begin(C_NAVY);
    s_lbl_modal_kicker = make_label(s_modal_container, "语音留言", &ecard_font_12, C_FAINT, 20, 28, 200,
                                    LV_TEXT_ALIGN_CENTER);
    s_lbl_modal_title = make_label(s_modal_container, "正在录音", &ecard_font_16, C_SURFACE, 20, 54,
                                   200, LV_TEXT_ALIGN_CENTER);
    make_box(s_modal_container, 76, 61, 7, 7, 0xEF4444, NO_BORDER, 4);

    /* 数字与单位用 flex 行居中，单位下移 9 px 后与 48 px 数字共享基线。 */
    lv_obj_t *count_row = make_box(s_modal_container, 20, 86, 200, 57, C_NAVY, NO_BORDER, 0);
    lv_obj_set_flex_flow(count_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(count_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(count_row, 3, 0);
    s_lbl_modal_big = make_label(count_row, "30", &ecard_font_48, C_SURFACE, 0, 0, 0,
                                 LV_TEXT_ALIGN_LEFT);
    lv_obj_t *unit = make_label(count_row, "秒", &ecard_font_16, C_FAINT, 0, 0, 0,
                                LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_translate_y(unit, -9, 0);

    create_waveform(s_modal_container, 222);
    modal_progress_bar(s_modal_container, 30, 238, 180, 30, 30);
    s_lbl_modal_hint = make_label(s_modal_container, "再按 OK 完成并发送", &ecard_font_12, C_SURFACE,
                                  20, 256, 200, LV_TEXT_ALIGN_CENTER);
    s_lbl_modal_sub = make_label(s_modal_container, "最长 15 秒", &ecard_font_12, C_FAINT, 20, 282,
                                 200, LV_TEXT_ALIGN_CENTER);
}

static void create_playing_modal(void) {
    modal_begin(C_NAVY);
    make_label(s_modal_container, "语音留言", &ecard_font_12, C_FAINT, 20, 28, 200,
               LV_TEXT_ALIGN_CENTER);
    make_box(s_modal_container, 82, 50, 76, 76, C_NAVY, 0x2C4F78, 38);
    lv_obj_t *avatar = make_box(s_modal_container, 92, 60, 56, 56, C_NAVY_SOFT, NO_BORDER, 28);
    make_label(avatar, "家", &ecard_font_22, 0x93C5FD, 0, 10, 56, LV_TEXT_ALIGN_CENTER);

    const ecard_profile_t *prof = ecard_profile_get();
    char title[64];
    snprintf(title, sizeof(title), "%s的留言", prof->voice.sender[0] ? prof->voice.sender : "家长");
    s_lbl_modal_title = make_label(s_modal_container, title, &ecard_font_16, C_SURFACE, 20, 128,
                                   200, LV_TEXT_ALIGN_CENTER);
    s_lbl_modal_sub = make_label(s_modal_container, "正在播放", &ecard_font_12, C_FAINT, 20, 154,
                                 200, LV_TEXT_ALIGN_CENTER);
    modal_progress_bar(s_modal_container, 30, 188, 180, 0, 1);
    s_lbl_modal_big = make_label(s_modal_container, "00:00 / 00:00", &ecard_font_12, C_SURFACE,
                                 30, 204, 180, LV_TEXT_ALIGN_CENTER);
    s_lbl_modal_hint = make_label(s_modal_container, "按 OK 停止", &ecard_font_12, C_FAINT, 20, 252,
                                  200, LV_TEXT_ALIGN_CENTER);
}

static void create_sos_pending_modal(void) {
    modal_begin(0x7F1D1D);
    make_label(s_modal_container, "紧急求助", &ecard_font_22, C_SURFACE, 20, 44, 200,
               LV_TEXT_ALIGN_CENTER);

    /* 细描边圆环把倒计时数字包成“警示按钮”，比裸数字更有紧迫感。 */
    lv_obj_t *ring = make_box(s_modal_container, 64, 82, 112, 112, 0x7F1D1D, 0xB91C1C, 56);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 2, 0);

    s_lbl_modal_big = make_label(s_modal_container, "3", &ecard_font_48, C_SURFACE, 0, 106, 240,
                                 LV_TEXT_ALIGN_CENTER);
    make_label(s_modal_container, "秒后发送求助", &ecard_font_16, C_SURFACE, 20, 202, 200,
               LV_TEXT_ALIGN_CENTER);
    s_lbl_modal_title = make_label(s_modal_container, "继续按住 DOWN 键", &ecard_font_16, C_SURFACE,
                                   20, 232, 200, LV_TEXT_ALIGN_CENTER);
    s_lbl_modal_hint = make_label(s_modal_container, "松开即可取消", &ecard_font_12, 0xFCA5A5,
                                  20, 264, 200, LV_TEXT_ALIGN_CENTER);
}

static void create_sos_sent_modal(void) {
    modal_begin(0x064E3B);

    /* 双层细环 + 实心徽标，让“已送达”的反馈更安定、更有完成感。 */
    lv_obj_t *outer = make_box(s_modal_container, 72, 64, 96, 96, 0x064E3B, 0x10B981, 48);
    lv_obj_set_style_bg_opa(outer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(outer, LV_OPA_30, 0);
    lv_obj_t *inner = make_box(s_modal_container, 82, 74, 76, 76, 0x064E3B, 0x10B981, 38);
    lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(inner, LV_OPA_50, 0);

    lv_obj_t *badge = make_box(s_modal_container, 92, 84, 56, 56, 0x065F46, NO_BORDER, 28);
    make_label(badge, "✓", &ecard_font_22, 0xA7F3D0, 0, 16, 56, LV_TEXT_ALIGN_CENTER);
    make_label(s_modal_container, "求助已发出", &ecard_font_22, C_SURFACE, 20, 156, 200,
               LV_TEXT_ALIGN_CENTER);
    make_label(s_modal_container, "请保持冷静", &ecard_font_16, 0xD1FAE5, 20, 196, 200,
               LV_TEXT_ALIGN_CENTER);
    make_label(s_modal_container, "老师与家长正在赶来", &ecard_font_12, 0xA7F3D0, 20, 222, 200,
               LV_TEXT_ALIGN_CENTER);
    s_lbl_modal_hint = make_label(s_modal_container, "按 OK 返回首页", &ecard_font_12, 0xD1FAE5,
                                  20, 276, 200, LV_TEXT_ALIGN_CENTER);
}

/* ------------------------------------------------------------------------- */
/* 动态刷新                                                                  */
/* ------------------------------------------------------------------------- */

static void update_home_content(void) {
    const ecard_profile_t *prof = ecard_profile_get();
    const ecard_message_t *latest_unread = ecard_messages_latest_unread();
    const int unread_count = ecard_messages_unread_count();
    const bool unread = unread_count > 0;

    if (s_panel_identity) {
        char name_class[96];
        snprintf(name_class, sizeof(name_class), "%s · %s", prof->student_name, prof->class_name);
        label_set_text(s_lbl_name_class, name_class);
        label_set_text(s_lbl_badge_text, role_badge_short(prof->role));
        lv_obj_set_style_bg_color(s_badge_capsule, color_from_rgb565(prof->theme.bg_color_rgb565), 0);
        lv_obj_set_style_border_color(s_badge_capsule,
                                      color_from_rgb565(prof->theme.border_color_rgb565), 0);
        lv_obj_set_style_text_color(s_lbl_badge_text,
                                    color_from_rgb565(prof->theme.text_color_rgb565), 0);
        if (unread) {
            lv_obj_add_flag(s_panel_identity, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_panel_identity, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_panel_unread) {
        if (unread && latest_unread) {
            char title[64];
            char action[64];
            snprintf(title, sizeof(title), "%s的语音留言", latest_unread->peer);
            if (unread_count > 1) {
                snprintf(action, sizeof(action), "共 %d 条未读 · 按 OK 收听", unread_count);
            } else {
                snprintf(action, sizeof(action), "%u 秒 · 按 OK 收听",
                         (unsigned)latest_unread->duration_sec);
            }
            label_set_text(s_lbl_unread_title, title);
            label_set_text(s_lbl_unread_action, action);
            lv_obj_clear_flag(s_panel_unread, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_panel_unread, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_lbl_hint) {
        label_set_text(s_lbl_hint, unread ? "按 OK 收听   ·   长按 OK 留言"
                                          : "OK 菜单 · 长按留言 · DOWN 求助");
    }
}

static void update_messages_content(void) {
    if (!s_msg_list || !s_msg_empty_panel) return;

    const int count = ecard_messages_recent_count();
    if (count <= 0) {
        lv_obj_add_flag(s_msg_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_msg_empty_panel, LV_OBJ_FLAG_HIDDEN);
        memset(s_msg_rows, 0, sizeof(s_msg_rows));
        return;
    }

    lv_obj_add_flag(s_msg_empty_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_msg_list, LV_OBJ_FLAG_HIDDEN);
    if (s_msg_selection >= (uint8_t)count) s_msg_selection = (uint8_t)(count - 1);

    // 行数少（最多 20 条），整表重建比维护增量状态更不容易出错。
    lv_obj_clean(s_msg_list);
    memset(s_msg_rows, 0, sizeof(s_msg_rows));

    for (int i = 0; i < count; ++i) {
        const ecard_message_t *msg = ecard_messages_recent(i);
        if (!msg) break;

        const bool selected = (i == (int)s_msg_selection);
        const bool sent = msg->direction == ECARD_MESSAGE_SENT;
        lv_obj_t *row = make_box(s_msg_list, 0, 0, 200, 48,
                                 selected ? C_BLUE_SOFT : C_SURFACE,
                                 selected ? C_BLUE : C_LINE, 12);
        if (selected) lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *badge = make_box(row, 8, 8, 32, 32,
                                   sent ? 0xFFF7E6 : C_BLUE_SOFT, NO_BORDER, 16);
        make_label(badge, sent ? "发" : "收", &ecard_font_16, sent ? 0xB45309 : C_BLUE, 0, 7, 32,
                   LV_TEXT_ALIGN_CENTER);

        static const lv_font_t *const peer_fonts[] = { &ecard_font_16, &ecard_font_12 };
        make_label(row, msg->peer, fit_label_font(msg->peer, 110, peer_fonts, 2), C_INK, 50,
                   5, 110, LV_TEXT_ALIGN_LEFT);

        char age[24];
        char info[48];
        ecard_messages_age_label(msg->created_ms, age, sizeof(age));
        snprintf(info, sizeof(info), "%u 秒 · %s", (unsigned)msg->duration_sec, age);
        make_label(row, info, &ecard_font_12, C_MUTED, 50, 26, 138, LV_TEXT_ALIGN_LEFT);

        // 未读圆点只出现在收到的留言上；发出的留言天然是已读。
        if (!sent && msg->unread) {
            make_box(row, 182, 21, 8, 8, C_BLUE, NO_BORDER, 4);
        }
        s_msg_rows[i] = row;
    }

    lv_obj_update_layout(s_msg_list);
    if (s_msg_rows[s_msg_selection]) {
        lv_obj_scroll_to_view(s_msg_rows[s_msg_selection], LV_ANIM_OFF);
    }
}

static void update_menu_voice_sub(void) {
    if (!s_lbl_menu_voice_sub) return;
    const int unread = ecard_messages_unread_count();
    char text[32];
    if (unread > 0) {
        snprintf(text, sizeof(text), "%d 条未读留言", unread);
    } else {
        snprintf(text, sizeof(text), "近 24 小时留言");
    }
    label_set_text(s_lbl_menu_voice_sub, text);
}

static void update_device_note(void) {
    if (!s_dev_note || !s_lbl_dev_note_title || !s_lbl_dev_note_sub ||
        !s_dev_note_badge) {
        return;
    }

    const char *title;
    const char *sub;
    uint32_t bg;
    uint32_t border;
    uint32_t fg;
    uint32_t badge;
    if (s_wifi_connected) {
        title = "紧急求助";
        sub = "长按 DOWN 键 3 秒";
        bg = C_RED_SOFT;
        border = 0xF6D5D5;
        fg = C_RED;
        badge = C_RED;
    } else if (s_net_retrying) {
        title = "正在重新连接…";
        sub = "请稍候，无需操作";
        bg = C_AMBER_BG;
        border = 0xF7DFB0;
        fg = 0xB45309;
        badge = C_AMBER;
    } else {
        title = "网络未连接";
        sub = "长按 UP 重试连接网络";
        bg = C_AMBER_BG;
        border = 0xF7DFB0;
        fg = 0xB45309;
        badge = C_AMBER;
    }

    label_set_text(s_lbl_dev_note_title, title);
    label_set_text(s_lbl_dev_note_sub, sub);
    lv_obj_set_style_bg_color(s_dev_note, lv_color_hex(bg), 0);
    lv_obj_set_style_border_color(s_dev_note, lv_color_hex(border), 0);
    lv_obj_set_style_bg_color(s_dev_note_badge, lv_color_hex(badge), 0);
    lv_obj_set_style_text_color(s_lbl_dev_note_title, lv_color_hex(fg), 0);
    lv_obj_set_style_text_color(s_lbl_dev_note_sub, lv_color_hex(fg), 0);
}

static void update_device_content(void) {
    if (s_lbl_dev_battery) {
        char bat[16];
        format_battery(bat, sizeof(bat));
        label_set_text(s_lbl_dev_battery, bat);
    }
    if (s_bar_dev_battery) {
        lv_bar_set_value(s_bar_dev_battery, s_battery_pct < 0 ? 0 : s_battery_pct, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_bar_dev_battery, lv_color_hex(battery_color()),
                                  LV_PART_INDICATOR);
    }
    if (s_lbl_dev_network) {
        label_set_text(s_lbl_dev_network, s_wifi_connected ? "校园网已连接" : "未连接");
    }
    update_device_note();
}

#endif /* HAS_LVGL_RUNTIME */

/* ------------------------------------------------------------------------- */
/* 公共 API                                                                  */
/* ------------------------------------------------------------------------- */

void ecard_ui_reset_navigation(void) {
    s_page = ECARD_UI_PAGE_HOME;
    s_modal = ECARD_UI_MODAL_NONE;
    s_menu_selection = 0;
    s_grade_selection = 0;
    s_deck_grade = 1;
    s_idle_elapsed_ms = 0;
    s_is_dimmed = false;
}

void ecard_ui_create(void) {
    s_idle_elapsed_ms = 0;
    s_is_dimmed = false;
    s_modal = ECARD_UI_MODAL_NONE;
    s_page = ECARD_UI_PAGE_HOME;
    s_menu_selection = 0;

#if HAS_LVGL_RUNTIME
    reset_page_pointers();
    create_home_page();
    update_home_content();
    lv_screen_load(s_screen);
#endif
}

void ecard_ui_destroy(void) {
#if HAS_LVGL_RUNTIME
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    reset_page_pointers();
#endif
    s_modal = ECARD_UI_MODAL_NONE;
}

void ecard_ui_show_page(ecard_ui_page_t page) {
    if (page < ECARD_UI_PAGE_HOME || page > ECARD_UI_PAGE_DEVICE) return;
    if (s_modal != ECARD_UI_MODAL_NONE) return;

    s_page = page;

#if HAS_LVGL_RUNTIME
    /* 先释放旧屏幕再创建新页面：避免菜单页(30+ 控件)与选年级页(53 控件)同时驻留 LVGL 池，
     * 消除跨页碎片导致的 lv_array_resize 分配失败断言死循环（ecard_input 看门狗卡死）。 */
    reset_page_pointers();
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }

    switch (page) {
    case ECARD_UI_PAGE_HOME:
        create_home_page();
        update_home_content();
        break;
    case ECARD_UI_PAGE_MENU:
        create_menu_page();
        update_menu_voice_sub();
        break;
    case ECARD_UI_PAGE_MESSAGES:
        create_messages_page();
        update_messages_content();
        break;
    case ECARD_UI_PAGE_QUIZ:
        create_quiz_page();
        update_quiz_content();
        break;
    case ECARD_UI_PAGE_POEM_PICK:
        create_pick_page(ECARD_CARD_POEM);
        break;
    case ECARD_UI_PAGE_WORD_PICK:
        create_pick_page(ECARD_CARD_WORD);
        break;
    case ECARD_UI_PAGE_POEM:
        create_flashcard_page(ECARD_CARD_POEM);
        update_flashcard_content();
        break;
    case ECARD_UI_PAGE_WORD:
        create_flashcard_page(ECARD_CARD_WORD);
        update_flashcard_content();
        break;
    case ECARD_UI_PAGE_POMODORO:
        create_pomodoro_page();
        break;
    case ECARD_UI_PAGE_SETUP:
        create_setup_page();
        break;
    case ECARD_UI_PAGE_RESET:
        create_reset_page();
        break;
    case ECARD_UI_PAGE_DEVICE:
        create_device_page();
        update_device_content();
        break;
    default:
        break;
    }

    if (s_screen) {
        lv_screen_load(s_screen);
    }
#endif
}

ecard_ui_page_t ecard_ui_get_page(void) {
    return s_page;
}

void ecard_ui_setup_refresh(void) {
#if HAS_LVGL_RUNTIME
    if (s_page != ECARD_UI_PAGE_SETUP || s_modal != ECARD_UI_MODAL_NONE) return;
    if (setup_stage() == s_setup_stage) return;
    ecard_ui_show_page(ECARD_UI_PAGE_SETUP);
#endif
}

void ecard_ui_set_deck_grade(uint8_t grade) {
    if (grade < 1) grade = 1;
    if (grade > ECARD_GRADE_COUNT) grade = ECARD_GRADE_COUNT;
    s_deck_grade = grade;
}

void ecard_ui_grade_move(int delta) {
    int count = ECARD_GRADE_COUNT;
    int next = ((int)s_grade_selection + delta) % count;
    if (next < 0) next += count;
    s_grade_selection = (uint8_t)next;
#if HAS_LVGL_RUNTIME
    apply_grade_selection();
#endif
}

uint8_t ecard_ui_grade_selection(void) {
    return s_grade_selection;
}

void ecard_ui_menu_move(int delta) {
    int count = (int)ECARD_MENU_COUNT;
    int next = ((int)s_menu_selection + delta) % count;
    if (next < 0) next += count;
    s_menu_selection = (uint8_t)next;
#if HAS_LVGL_RUNTIME
    apply_menu_selection();
#endif
}

uint8_t ecard_ui_menu_selection(void) {
    return s_menu_selection;
}

uint8_t ecard_ui_messages_selection(void) {
    return s_msg_selection;
}

void ecard_ui_messages_move(int delta) {
    const int count = ecard_messages_recent_count();
    if (count <= 0) return;

    int next = (int)s_msg_selection + delta;
    while (next < 0) next += count;
    while (next >= count) next -= count;
    s_msg_selection = (uint8_t)next;
#if HAS_LVGL_RUNTIME
    update_messages_content();
#endif
}

void ecard_ui_update_status(const char *time_str, int battery_pct, bool is_charging,
                            bool wifi_connected) {
    if (time_str) {
        snprintf(s_time_str, sizeof(s_time_str), "%s", time_str);
    }
    s_battery_pct = battery_pct;
    s_is_charging = is_charging;
    s_wifi_connected = wifi_connected;

#if HAS_LVGL_RUNTIME
    if (s_lbl_time) {
        label_set_text(s_lbl_time, s_time_str);
    }
    if (s_lbl_battery) {
        char bat[16];
        format_battery(bat, sizeof(bat));
        label_set_text(s_lbl_battery, bat);
    }
    update_battery_icon();
    update_wifi_icon();
    update_device_content();
#else
    (void)time_str;
#endif
}

void ecard_ui_set_ai_connected(bool connected) {
    if (s_ai_connected == connected) return;
    s_ai_connected = connected;
#if HAS_LVGL_RUNTIME
    update_ai_icon();
#endif
}

void ecard_ui_refresh_content(void) {
#if HAS_LVGL_RUNTIME
    if (s_page == ECARD_UI_PAGE_HOME) {
        update_home_content();
    } else if (s_page == ECARD_UI_PAGE_MESSAGES) {
        update_messages_content();
    } else if (s_page == ECARD_UI_PAGE_MENU) {
        update_menu_voice_sub();
    } else if (s_page == ECARD_UI_PAGE_QUIZ) {
        update_quiz_content();
    }
#endif
}

void ecard_ui_set_network_retrying(bool retrying) {
    s_net_retrying = retrying;
#if HAS_LVGL_RUNTIME
    update_device_content();
#endif
}

void ecard_ui_quiz_refresh(void) {
#if HAS_LVGL_RUNTIME
    if (s_page == ECARD_UI_PAGE_QUIZ) {
        update_quiz_content();
    }
#endif
}

void ecard_ui_flashcard_refresh(void) {
#if HAS_LVGL_RUNTIME
    if (s_page == ECARD_UI_PAGE_POEM || s_page == ECARD_UI_PAGE_WORD) {
        update_flashcard_content();
    }
#endif
}

void ecard_ui_pomodoro_refresh(void) {
#if HAS_LVGL_RUNTIME
    if (s_page == ECARD_UI_PAGE_POMODORO) {
        update_pomodoro_content();
    }
#endif
}

void ecard_ui_show_recording_modal(uint8_t remaining_sec, uint8_t audio_amplitude) {
#if HAS_LVGL_RUNTIME
    const bool needs_create = (s_modal != ECARD_UI_MODAL_RECORDING) || !s_modal_container;
#endif
    s_modal = ECARD_UI_MODAL_RECORDING;
#if HAS_LVGL_RUNTIME
    if (needs_create) {
        create_recording_modal();
    }
    if (s_lbl_modal_kicker) label_set_text(s_lbl_modal_kicker, "语音留言");
    if (s_lbl_modal_title) label_set_text(s_lbl_modal_title, "正在录音");
    if (s_lbl_modal_hint) label_set_text(s_lbl_modal_hint, "再按 OK 完成并发送");
    if (s_lbl_modal_sub) label_set_text(s_lbl_modal_sub, "最长 10 秒");
    if (s_lbl_modal_big) {
        char sec[8];
        snprintf(sec, sizeof(sec), "%u", (unsigned)remaining_sec);
        label_set_text(s_lbl_modal_big, sec);
    }
    if (s_bar_modal) {
        lv_bar_set_range(s_bar_modal, 0, ECARD_VOICE_MAX_SEC);
        lv_bar_set_value(s_bar_modal, remaining_sec, LV_ANIM_OFF);
    }
    update_waveform(audio_amplitude);
#else
    (void)remaining_sec;
    (void)audio_amplitude;
#endif
}

void ecard_ui_show_ai_listen_modal(uint8_t remaining_sec, uint8_t audio_amplitude) {
    // 复用录音浮层；s_modal 在无 LVGL 的宿主测试里也要更新，因此放在条件编译外。
    ecard_ui_show_recording_modal(remaining_sec, audio_amplitude);
#if HAS_LVGL_RUNTIME
    // 长按 UP 必须有明确反馈：顶部说明这是小智追问，不是普通留言录音。
    if (s_lbl_modal_kicker) label_set_text(s_lbl_modal_kicker, "AI 学习助手");
    // 避开生僻字（该字不在 GB2312 一级字库），保证内置点阵包含全部界面文字。
    if (s_lbl_modal_title) label_set_text(s_lbl_modal_title, "小智正在听");
    if (s_lbl_modal_hint) label_set_text(s_lbl_modal_hint, "松开 UP 发送 · 说完再松手");
    if (s_lbl_modal_sub) label_set_text(s_lbl_modal_sub, "最长 10 秒");
    if (s_bar_modal) {
        lv_bar_set_range(s_bar_modal, 0, 10);
        lv_bar_set_value(s_bar_modal, remaining_sec > 10 ? 10 : remaining_sec,
                         LV_ANIM_OFF);
    }
#else
    (void)remaining_sec;
    (void)audio_amplitude;
#endif
}

void ecard_ui_set_hint(const char *text) {
#if HAS_LVGL_RUNTIME
    if (s_lbl_hint && text) {
        label_set_text(s_lbl_hint, text);
    }
#else
    (void)text;
#endif
}

void ecard_ui_show_playing_modal(uint8_t current_sec, uint8_t total_sec) {
#if HAS_LVGL_RUNTIME
    const bool needs_create = (s_modal != ECARD_UI_MODAL_PLAYING) || !s_modal_container;
#endif
    s_modal = ECARD_UI_MODAL_PLAYING;
#if HAS_LVGL_RUNTIME
    if (needs_create) {
        create_playing_modal();
    }
    if (s_bar_modal) {
        lv_bar_set_range(s_bar_modal, 0, total_sec > 0 ? total_sec : 1);
        lv_bar_set_value(s_bar_modal, current_sec, LV_ANIM_OFF);
    }
    if (s_lbl_modal_big) {
        char buf[32];
        snprintf(buf, sizeof(buf), "00:%02u / 00:%02u", (unsigned)current_sec,
                 (unsigned)total_sec);
        label_set_text(s_lbl_modal_big, buf);
    }
#else
    (void)current_sec;
    (void)total_sec;
#endif
}

void ecard_ui_show_sos_pending(uint8_t seconds_left) {
#if HAS_LVGL_RUNTIME
    const bool needs_create = (s_modal != ECARD_UI_MODAL_SOS_PENDING) || !s_modal_container;
#endif
    s_modal = ECARD_UI_MODAL_SOS_PENDING;
#if HAS_LVGL_RUNTIME
    if (needs_create) {
        create_sos_pending_modal();
    }
    if (s_lbl_modal_big) {
        char sec[8];
        snprintf(sec, sizeof(sec), "%u", (unsigned)seconds_left);
        label_set_text(s_lbl_modal_big, sec);
    }
#else
    (void)seconds_left;
#endif
}

void ecard_ui_show_sos_sent(void) {
#if HAS_LVGL_RUNTIME
    const bool needs_create = (s_modal != ECARD_UI_MODAL_SOS_SENT) || !s_modal_container;
#endif
    s_modal = ECARD_UI_MODAL_SOS_SENT;
#if HAS_LVGL_RUNTIME
    if (needs_create) {
        create_sos_sent_modal();
    }
#endif
}

void ecard_ui_hide_modal(void) {
    s_modal = ECARD_UI_MODAL_NONE;
#if HAS_LVGL_RUNTIME
    if (s_modal_container) {
        lv_obj_delete(s_modal_container);
        reset_modal_pointers();
    }
#endif
}

ecard_ui_modal_t ecard_ui_get_modal(void) {
    return s_modal;
}

void ecard_ui_feed_activity(void) {
    s_idle_elapsed_ms = 0;
    s_is_dimmed = false;
}

bool ecard_ui_check_idle_timeout(uint32_t delta_ms) {
    if (s_modal != ECARD_UI_MODAL_NONE) {
        s_idle_elapsed_ms = 0;
        return false;
    }
    s_idle_elapsed_ms += delta_ms;
    if (s_idle_elapsed_ms >= ECARD_IDLE_TIMEOUT_MS && !s_is_dimmed) {
        s_is_dimmed = true;
        return true;
    }
    return false;
}
