#include "ecard_ui.h"
#include "ecard_barcode.h"
#include <stdio.h>
#include <string.h>

#if defined(ESP_PLATFORM) || defined(LV_CONF_INCLUDE_SIMPLE) || defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#define HAS_LVGL_RUNTIME 1
#else
// 主机轻量桩/非ESP环境桩
#define HAS_LVGL_RUNTIME 0
typedef void *lv_obj_t;
#endif

// 2 分钟无操作空闲超时 (120,000 ms)
#define ECARD_IDLE_TIMEOUT_MS (120 * 1000)

static ecard_ui_modal_t s_current_modal = ECARD_UI_MODAL_NONE;
static uint32_t s_idle_elapsed_ms = 0;
static bool s_is_screen_dimmed = false;

#if HAS_LVGL_RUNTIME
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_lbl_wifi = NULL;
static lv_obj_t *s_lbl_time = NULL;
static lv_obj_t *s_lbl_battery = NULL;
static lv_obj_t *s_lbl_school = NULL;

static lv_obj_t *s_body_panel = NULL;
static lv_obj_t *s_barcode_canvas = NULL;
static lv_obj_t *s_lbl_student_id = NULL;
static lv_obj_t *s_photo_box = NULL;

static lv_obj_t *s_panel_identity = NULL;
static lv_obj_t *s_lbl_name_class = NULL;
static lv_obj_t *s_badge_capsule = NULL;
static lv_obj_t *s_lbl_badge_text = NULL;

static lv_obj_t *s_panel_unread = NULL;
static lv_obj_t *s_lbl_unread_title = NULL;
static lv_obj_t *s_lbl_unread_action = NULL;

static lv_obj_t *s_lbl_bottom_tips = NULL;
static lv_obj_t *s_modal_container = NULL;
static lv_obj_t *s_lbl_modal_title = NULL;
static lv_obj_t *s_lbl_modal_detail = NULL;

// 纵向条形码画布像素缓存 (80x150)
static uint16_t s_barcode_buf[80 * 150];
#endif

void ecard_ui_create(void) {
    s_idle_elapsed_ms = 0;
    s_is_screen_dimmed = false;
    s_current_modal = ECARD_UI_MODAL_NONE;

#if HAS_LVGL_RUNTIME
    const ecard_profile_t *prof = ecard_profile_get();

    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, 240, 320);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // 1. 顶部状态栏 (高度 22px)
    lv_obj_t *status_bar = lv_obj_create(s_screen);
    lv_obj_set_size(status_bar, 240, 22);
    lv_obj_set_pos(status_bar, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0xF3F4F6), 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 2, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_wifi = lv_label_create(status_bar);
    lv_label_set_text(s_lbl_wifi, "WiFi:OFF");
    lv_obj_align(s_lbl_wifi, LV_ALIGN_LEFT_MID, 6, 0);

    s_lbl_time = lv_label_create(status_bar);
    lv_label_set_text(s_lbl_time, "10:15");
    lv_obj_align(s_lbl_time, LV_ALIGN_CENTER, 0, 0);

    s_lbl_battery = lv_label_create(status_bar);
    lv_label_set_text(s_lbl_battery, "88%");
    lv_obj_align(s_lbl_battery, LV_ALIGN_RIGHT_MID, -6, 0);

    // 2. 校名标头 (高度 20px)
    s_lbl_school = lv_label_create(s_screen);
    lv_label_set_text(s_lbl_school, prof->school_name);
    lv_obj_set_pos(s_lbl_school, 0, 24);
    lv_obj_set_width(s_lbl_school, 240);
    lv_obj_set_style_text_align(s_lbl_school, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_lbl_school, lv_color_hex(0x1F2937), 0);

    // 3. 中部容器：左侧纵向条码 + 右侧 125x175 标准证件照
    s_body_panel = lv_obj_create(s_screen);
    lv_obj_set_size(s_body_panel, 230, 175);
    lv_obj_set_pos(s_body_panel, 5, 46);
    lv_obj_set_style_bg_opa(s_body_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_body_panel, 0, 0);
    lv_obj_set_style_pad_all(s_body_panel, 0, 0);
    lv_obj_clear_flag(s_body_panel, LV_OBJ_FLAG_SCROLLABLE);

    // 3.1 左侧条码区 (宽 85, 高 175)
    ecard_barcode_render_vertical_rgb565(prof->student_id, s_barcode_buf, 80, 150, 0x0000, 0xFFFF);
    s_barcode_canvas = lv_image_create(s_body_panel);
    // 配置图像缓冲区
    lv_obj_set_pos(s_barcode_canvas, 2, 0);
    lv_obj_set_size(s_barcode_canvas, 80, 150);

    s_lbl_student_id = lv_label_create(s_body_panel);
    lv_label_set_text(s_lbl_student_id, prof->student_id);
    lv_obj_set_pos(s_lbl_student_id, 2, 155);
    lv_obj_set_width(s_lbl_student_id, 80);
    lv_obj_set_style_text_align(s_lbl_student_id, LV_TEXT_ALIGN_CENTER, 0);

    // 3.2 右侧证件照区 (宽 125, 高 175, 5:7 黄金比例)
    s_photo_box = lv_obj_create(s_body_panel);
    lv_obj_set_size(s_photo_box, 125, 175);
    lv_obj_set_pos(s_photo_box, 100, 0);
    lv_obj_set_style_bg_color(s_photo_box, lv_color_hex(0xE5E7EB), 0);
    lv_obj_set_style_border_color(s_photo_box, lv_color_hex(0xD1D5DB), 0);
    lv_obj_set_style_border_width(s_photo_box, 2, 0);
    lv_obj_set_style_radius(s_photo_box, 6, 0);
    lv_obj_clear_flag(s_photo_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_photo_placeholder = lv_label_create(s_photo_box);
    lv_label_set_text(lbl_photo_placeholder, "证件相片");
    lv_obj_align(lbl_photo_placeholder, LV_ALIGN_CENTER, 0, 0);

    // 4. 下方互斥容器 (Y: 226, 高度 64)
    // 4.1 状态 A: 姓名班级 + 身份胶囊徽章
    s_panel_identity = lv_obj_create(s_screen);
    lv_obj_set_size(s_panel_identity, 230, 64);
    lv_obj_set_pos(s_panel_identity, 5, 226);
    lv_obj_set_style_border_width(s_panel_identity, 0, 0);
    lv_obj_set_style_bg_opa(s_panel_identity, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_panel_identity, 0, 0);
    lv_obj_clear_flag(s_panel_identity, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_name_class = lv_label_create(s_panel_identity);
    char name_class_buf[64];
    snprintf(name_class_buf, sizeof(name_class_buf), "%s  ·  %s", prof->student_name, prof->class_name);
    lv_label_set_text(s_lbl_name_class, name_class_buf);
    lv_obj_align(s_lbl_name_class, LV_ALIGN_TOP_MID, 0, 2);

    // 身份徽章胶囊 (圆角高光底色)
    s_badge_capsule = lv_obj_create(s_panel_identity);
    lv_obj_set_size(s_badge_capsule, 160, 26);
    lv_obj_align(s_badge_capsule, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_radius(s_badge_capsule, 13, 0); // 胶囊圆角
    lv_obj_set_style_bg_color(s_badge_capsule, lv_color_hex(0xE0F2FE), 0);
    lv_obj_set_style_border_color(s_badge_capsule, lv_color_hex(0xBAE6FD), 0);
    lv_obj_set_style_border_width(s_badge_capsule, 1, 0);
    lv_obj_clear_flag(s_badge_capsule, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_badge_text = lv_label_create(s_badge_capsule);
    lv_label_set_text(s_lbl_badge_text, "★ 学生 STUDENT ★");
    lv_obj_set_style_text_color(s_lbl_badge_text, lv_color_hex(0x0369A1), 0);
    lv_obj_align(s_lbl_badge_text, LV_ALIGN_CENTER, 0, 0);

    // 4.2 状态 B: 未读消息高亮提醒卡片
    s_panel_unread = lv_obj_create(s_screen);
    lv_obj_set_size(s_panel_unread, 230, 64);
    lv_obj_set_pos(s_panel_unread, 5, 226);
    lv_obj_set_style_radius(s_panel_unread, 8, 0);
    lv_obj_set_style_bg_color(s_panel_unread, lv_color_hex(0xFEF3C7), 0); // 暖黄底色
    lv_obj_set_style_border_color(s_panel_unread, lv_color_hex(0xF59E0B), 0); // 橙黄高亮边框
    lv_obj_set_style_border_width(s_panel_unread, 2, 0);
    lv_obj_set_style_pad_all(s_panel_unread, 4, 0);
    lv_obj_clear_flag(s_panel_unread, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_unread_title = lv_label_create(s_panel_unread);
    lv_label_set_text(s_lbl_unread_title, "新语音留言");
    lv_obj_set_style_text_color(s_lbl_unread_title, lv_color_hex(0xB45309), 0);
    lv_obj_align(s_lbl_unread_title, LV_ALIGN_TOP_MID, 0, 4);

    s_lbl_unread_action = lv_label_create(s_panel_unread);
    lv_label_set_text(s_lbl_unread_action, "按 OK 键收听");
    lv_obj_set_style_text_color(s_lbl_unread_action, lv_color_hex(0x92400E), 0);
    lv_obj_align(s_lbl_unread_action, LV_ALIGN_BOTTOM_MID, 0, -4);

    // 5. 底部微型操作指引 (Y: 295, 高度 20px)
    s_lbl_bottom_tips = lv_label_create(s_screen);
    lv_label_set_text(s_lbl_bottom_tips, "长按OK留言 | 长按DOWN报警");
    lv_obj_set_pos(s_lbl_bottom_tips, 0, 298);
    lv_obj_set_width(s_lbl_bottom_tips, 240);
    lv_obj_set_style_text_align(s_lbl_bottom_tips, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_lbl_bottom_tips, lv_color_hex(0x9CA3AF), 0);

    // 刷新互斥显示初始状态
    ecard_ui_refresh_content();

    lv_screen_load(s_screen);
#endif
}

void ecard_ui_destroy(void) {
#if HAS_LVGL_RUNTIME
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
#endif
}

void ecard_ui_update_status(const char *time_str, uint8_t battery_pct, bool is_charging, bool wifi_connected) {
#if HAS_LVGL_RUNTIME
    if (s_lbl_time && time_str) {
        lv_label_set_text(s_lbl_time, time_str);
    }
    if (s_lbl_battery) {
        char bat_buf[16];
        snprintf(bat_buf, sizeof(bat_buf), "%s%d%%", is_charging ? "+" : "", battery_pct);
        lv_label_set_text(s_lbl_battery, bat_buf);
    }
    if (s_lbl_wifi) {
        lv_label_set_text(s_lbl_wifi, wifi_connected ? "WiFi:ON" : "WiFi:OFF");
    }
#else
    (void)time_str;
    (void)battery_pct;
    (void)is_charging;
    (void)wifi_connected;
#endif
}

void ecard_ui_refresh_content(void) {
#if HAS_LVGL_RUNTIME
    bool has_msg = ecard_profile_has_unread();
    const ecard_profile_t *prof = ecard_profile_get();

    if (has_msg) {
        // 互斥：隐藏身份面板，展示未读留言卡片
        if (s_panel_identity) lv_obj_add_flag(s_panel_identity, LV_OBJ_FLAG_HIDDEN);
        if (s_panel_unread) {
            lv_obj_clear_flag(s_panel_unread, LV_OBJ_FLAG_HIDDEN);
            char title_buf[64];
            snprintf(title_buf, sizeof(title_buf), "%s (%ds)", prof->voice.sender, prof->voice.duration_sec);
            lv_label_set_text(s_lbl_unread_title, title_buf);
        }
    } else {
        // 互斥复原：展示身份面板，隐藏未读留言卡片
        if (s_panel_unread) lv_obj_add_flag(s_panel_unread, LV_OBJ_FLAG_HIDDEN);
        if (s_panel_identity) {
            lv_obj_clear_flag(s_panel_identity, LV_OBJ_FLAG_HIDDEN);

            // 更新身份徽章色彩 (方案 A 或 方案 B)
            if (s_badge_capsule && s_lbl_badge_text) {
                // 根据主题设置色彩
                lv_obj_set_style_bg_color(s_badge_capsule, lv_color_hex(prof->theme.bg_color_rgb565), 0);
                lv_obj_set_style_text_color(s_lbl_badge_text, lv_color_hex(prof->theme.text_color_rgb565), 0);
            }
        }
    }
#endif
}

void ecard_ui_show_recording_modal(uint8_t remaining_sec, uint8_t audio_amplitude) {
    s_current_modal = ECARD_UI_MODAL_RECORDING;
#if HAS_LVGL_RUNTIME
    if (!s_modal_container && s_screen) {
        s_modal_container = lv_obj_create(s_screen);
        lv_obj_set_size(s_modal_container, 200, 140);
        lv_obj_align(s_modal_container, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_radius(s_modal_container, 12, 0);
        lv_obj_set_style_bg_color(s_modal_container, lv_color_hex(0x111827), 0);
        lv_obj_set_style_border_color(s_modal_container, lv_color_hex(0x3B82F6), 0);
        lv_obj_set_style_border_width(s_modal_container, 2, 0);
        lv_obj_clear_flag(s_modal_container, LV_OBJ_FLAG_SCROLLABLE);

        s_lbl_modal_title = lv_label_create(s_modal_container);
        lv_label_set_text(s_lbl_modal_title, "正在录音...");
        lv_obj_set_style_text_color(s_lbl_modal_title, lv_color_hex(0xF9FAFB), 0);
        lv_obj_align(s_lbl_modal_title, LV_ALIGN_TOP_MID, 0, 8);

        s_lbl_modal_detail = lv_label_create(s_modal_container);
        lv_obj_set_style_text_color(s_lbl_modal_detail, lv_color_hex(0x93C5FD), 0);
        lv_obj_align(s_lbl_modal_detail, LV_ALIGN_CENTER, 0, 10);
    }

    if (s_lbl_modal_detail) {
        char detail_buf[64];
        snprintf(detail_buf, sizeof(detail_buf), "剩余: %ds\n振幅: %d%%\n短按OK提前结束", remaining_sec, audio_amplitude);
        lv_label_set_text(s_lbl_modal_detail, detail_buf);
    }
#else
    (void)remaining_sec;
    (void)audio_amplitude;
#endif
}

void ecard_ui_show_playing_modal(uint8_t current_sec, uint8_t total_sec) {
    s_current_modal = ECARD_UI_MODAL_PLAYING;
#if HAS_LVGL_RUNTIME
    if (!s_modal_container && s_screen) {
        s_modal_container = lv_obj_create(s_screen);
        lv_obj_set_size(s_modal_container, 200, 120);
        lv_obj_align(s_modal_container, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_radius(s_modal_container, 12, 0);
        lv_obj_set_style_bg_color(s_modal_container, lv_color_hex(0x1F2937), 0);
        lv_obj_set_style_border_color(s_modal_container, lv_color_hex(0x10B981), 0);
        lv_obj_set_style_border_width(s_modal_container, 2, 0);
        lv_obj_clear_flag(s_modal_container, LV_OBJ_FLAG_SCROLLABLE);

        s_lbl_modal_title = lv_label_create(s_modal_container);
        lv_label_set_text(s_lbl_modal_title, "正在播放留言");
        lv_obj_set_style_text_color(s_lbl_modal_title, lv_color_hex(0xF9FAFB), 0);
        lv_obj_align(s_lbl_modal_title, LV_ALIGN_TOP_MID, 0, 8);

        s_lbl_modal_detail = lv_label_create(s_modal_container);
        lv_obj_set_style_text_color(s_lbl_modal_detail, lv_color_hex(0xA7F3D0), 0);
        lv_obj_align(s_lbl_modal_detail, LV_ALIGN_CENTER, 0, 10);
    }

    if (s_lbl_modal_detail) {
        char detail_buf[48];
        snprintf(detail_buf, sizeof(detail_buf), "%02d / %02d 秒\n短按OK停止", current_sec, total_sec);
        lv_label_set_text(s_lbl_modal_detail, detail_buf);
    }
#else
    (void)current_sec;
    (void)total_sec;
#endif
}

void ecard_ui_hide_modal(void) {
    s_current_modal = ECARD_UI_MODAL_NONE;
#if HAS_LVGL_RUNTIME
    if (s_modal_container) {
        lv_obj_delete(s_modal_container);
        s_modal_container = NULL;
        s_lbl_modal_title = NULL;
        s_lbl_modal_detail = NULL;
    }
#endif
}

ecard_ui_modal_t ecard_ui_get_modal(void) {
    return s_current_modal;
}

void ecard_ui_feed_activity(void) {
    s_idle_elapsed_ms = 0;
    if (s_is_screen_dimmed) {
        s_is_screen_dimmed = false;
        // 唤醒点亮屏幕 (在真实设备端恢复 100% 背光)
    }
}

bool ecard_ui_check_idle_timeout(uint32_t delta_ms) {
    s_idle_elapsed_ms += delta_ms;
    if (s_idle_elapsed_ms >= ECARD_IDLE_TIMEOUT_MS) {
        if (!s_is_screen_dimmed) {
            s_is_screen_dimmed = true;
            return true; // 触发 2 分钟无操作浅休眠
        }
    }
    return false;
}
