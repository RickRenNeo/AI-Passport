#include "ecard_profile.h"
#include <string.h>

// 静态单例保存当前 profile 状态
static ecard_profile_t s_current_profile;
static bool s_profile_inited = false;

// 方案 A 预置颜色转换辅助 (RGB888 -> RGB565)
#define RGB565(r, g, b) (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

ecard_badge_theme_t ecard_theme_get_preset_a(ecard_role_t role) {
    ecard_badge_theme_t theme;
    memset(&theme, 0, sizeof(theme));
    theme.is_customized = false;

    switch (role) {
    case ECARD_ROLE_STUDENT:
        // 学生：活力蓝灰底 (#E0F2FE: 224, 242, 254) + 深靛蓝字 (#0369A1: 3, 105, 161)
        theme.bg_color_rgb565 = RGB565(224, 242, 254);
        theme.text_color_rgb565 = RGB565(3, 105, 161);
        theme.border_color_rgb565 = RGB565(186, 230, 253); // #BAE6FD
        theme.watermark = ECARD_WATERMARK_EMBLEM;
        break;

    case ECARD_ROLE_FACULTY:
        // 教师：雅致曜石金底 (#FEF3C7: 254, 243, 199) + 绛金棕字 (#92400E: 146, 64, 14)
        theme.bg_color_rgb565 = RGB565(254, 243, 199);
        theme.text_color_rgb565 = RGB565(146, 64, 14);
        theme.border_color_rgb565 = RGB565(253, 230, 138); // #FDE68A
        theme.watermark = ECARD_WATERMARK_STRIPE;
        break;

    case ECARD_ROLE_STAFF:
    default:
        // 教工/访客：沉稳素雅绿 (#DCFCE7: 220, 252, 231) + 墨绿字 (#166534: 22, 101, 52)
        theme.bg_color_rgb565 = RGB565(220, 252, 231);
        theme.text_color_rgb565 = RGB565(22, 101, 52);
        theme.border_color_rgb565 = RGB565(187, 247, 208);
        theme.watermark = ECARD_WATERMARK_GRADE;
        break;
    }

    return theme;
}

void ecard_profile_init_default(void) {
    memset(&s_current_profile, 0, sizeof(s_current_profile));

    strncpy(s_current_profile.school_name, "XX市第一实验学校", sizeof(s_current_profile.school_name) - 1);
    strncpy(s_current_profile.student_name, "张小明", sizeof(s_current_profile.student_name) - 1);
    strncpy(s_current_profile.class_name, "初二 (3) 班", sizeof(s_current_profile.class_name) - 1);
    strncpy(s_current_profile.student_id, "2026110508", sizeof(s_current_profile.student_id) - 1);

    s_current_profile.role = ECARD_ROLE_STUDENT;
    s_current_profile.theme = ecard_theme_get_preset_a(s_current_profile.role);

    // 默认无未读消息
    s_current_profile.voice.has_unread = false;
    s_current_profile.voice.duration_sec = 0;
    s_current_profile.voice.sender[0] = '\0';
    s_current_profile.voice.timestamp = 0;

    s_profile_inited = true;
}

const ecard_profile_t *ecard_profile_get(void) {
    if (!s_profile_inited) {
        ecard_profile_init_default();
    }
    return &s_current_profile;
}

void ecard_profile_set_custom_theme(const ecard_badge_theme_t *custom_theme) {
    if (!s_profile_inited) {
        ecard_profile_init_default();
    }
    if (custom_theme) {
        s_current_profile.theme = *custom_theme;
        s_current_profile.theme.is_customized = true; // 标记方案 B 生效
    }
}

void ecard_profile_set_unread_voice(const char *sender, uint8_t duration_sec) {
    if (!s_profile_inited) {
        ecard_profile_init_default();
    }
    s_current_profile.voice.has_unread = true;
    s_current_profile.voice.duration_sec = duration_sec;
    if (sender) {
        strncpy(s_current_profile.voice.sender, sender, sizeof(s_current_profile.voice.sender) - 1);
        s_current_profile.voice.sender[sizeof(s_current_profile.voice.sender) - 1] = '\0';
    } else {
        strncpy(s_current_profile.voice.sender, "来自家长", sizeof(s_current_profile.voice.sender) - 1);
    }
}

void ecard_profile_clear_unread_voice(void) {
    if (!s_profile_inited) {
        ecard_profile_init_default();
    }
    s_current_profile.voice.has_unread = false;
    s_current_profile.voice.duration_sec = 0;
    s_current_profile.voice.sender[0] = '\0';
}

bool ecard_profile_has_unread(void) {
    if (!s_profile_inited) {
        ecard_profile_init_default();
    }
    return s_current_profile.voice.has_unread;
}
