#include "ecard_profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_profile_defaults(void) {
    ecard_profile_init_default();
    const ecard_profile_t *p = ecard_profile_get();
    assert(p != NULL);
    assert(strcmp(p->student_name, "张小明") == 0);
    assert(strcmp(p->class_name, "初二 (3) 班") == 0);
    assert(strcmp(p->student_id, "2026110508") == 0);
    assert(p->role == ECARD_ROLE_STUDENT);
    assert(!p->theme.is_customized); // 方案 A 预置
    assert(p->theme.watermark == ECARD_WATERMARK_EMBLEM);
    assert(!ecard_profile_has_unread());
}

static void test_profile_unread_mutual_exclusion(void) {
    ecard_profile_init_default();
    assert(!ecard_profile_has_unread());

    // 收到未读留言
    ecard_profile_set_unread_voice("妈妈", 15);
    assert(ecard_profile_has_unread());
    const ecard_profile_t *p = ecard_profile_get();
    assert(p->voice.has_unread);
    assert(p->voice.duration_sec == 15);
    assert(strcmp(p->voice.sender, "妈妈") == 0);

    // 播放完毕清除 -> 互斥复原
    ecard_profile_clear_unread_voice();
    assert(!ecard_profile_has_unread());
}

static void test_profile_custom_theme_plan_b(void) {
    ecard_profile_init_default();

    ecard_badge_theme_t custom;
    custom.bg_color_rgb565 = 0x1234;
    custom.text_color_rgb565 = 0x5678;
    custom.border_color_rgb565 = 0x9ABC;
    custom.watermark = ECARD_WATERMARK_GRADE;
    custom.is_customized = true;

    ecard_profile_set_custom_theme(&custom);

    const ecard_profile_t *p = ecard_profile_get();
    assert(p->theme.is_customized);
    assert(p->theme.bg_color_rgb565 == 0x1234);
    assert(p->theme.watermark == ECARD_WATERMARK_GRADE);
}

int main(void) {
    printf("Running ecard_profile tests...\n");
    test_profile_defaults();
    test_profile_unread_mutual_exclusion();
    test_profile_custom_theme_plan_b();
    printf("ecard_profile tests passed!\n");
    return 0;
}
