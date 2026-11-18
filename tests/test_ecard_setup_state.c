#include "ecard_setup.h"
#include "ecard_cards.h"
#include "ecard_profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_two_step_provisioning(void) {
    ecard_setup_init();
    assert(!ecard_setup_has_wifi());
    assert(!ecard_setup_has_identity());
    assert(!ecard_setup_is_provisioned());

    // 第一步：只保存 Wi-Fi，绝不能把设备判定为“已完成配置”。
    assert(ecard_setup_save_wifi("HomeNet", "password123"));
    assert(ecard_setup_has_wifi());
    assert(!ecard_setup_has_identity());
    assert(!ecard_setup_is_provisioned());

    // 第二步：保存身份后才算完成。
    ecard_setup_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.role = ECARD_ACCOUNT_STUDENT;
    snprintf(cfg.id, sizeof(cfg.id), "20260101");
    snprintf(cfg.school, sizeof(cfg.school), "XX市第一实验学校");
    snprintf(cfg.class_name, sizeof(cfg.class_name), "一(8)班");
    snprintf(cfg.name, sizeof(cfg.name), "张小明");
    snprintf(cfg.phone, sizeof(cfg.phone), "13800138000");
    assert(ecard_setup_save_identity(&cfg));
    assert(ecard_setup_has_identity());
    assert(ecard_setup_is_provisioned());
}

static void test_teacher_identity(void) {
    ecard_setup_init();
    assert(ecard_setup_save_wifi("HomeNet", "password123"));

    ecard_setup_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.role = ECARD_ACCOUNT_TEACHER;
    snprintf(cfg.id, sizeof(cfg.id), "T2026");
    snprintf(cfg.school, sizeof(cfg.school), "XX市第一实验学校");
    snprintf(cfg.name, sizeof(cfg.name), "王老师");
    snprintf(cfg.phone, sizeof(cfg.phone), "13900139000");
    assert(ecard_setup_save_identity(&cfg));
    assert(ecard_setup_has_identity());
    assert(ecard_setup_is_provisioned());
}

static void test_invalid_identity_keeps_wifi(void) {
    ecard_setup_init();
    assert(ecard_setup_save_wifi("HomeNet", "password123"));

    ecard_setup_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.role = ECARD_ACCOUNT_STUDENT;
    snprintf(cfg.id, sizeof(cfg.id), "123");  // 不是 8 位
    snprintf(cfg.school, sizeof(cfg.school), "XX市第一实验学校");
    snprintf(cfg.class_name, sizeof(cfg.class_name), "一(8)班");
    snprintf(cfg.name, sizeof(cfg.name), "张小明");
    snprintf(cfg.phone, sizeof(cfg.phone), "13800138000");
    assert(!ecard_setup_save_identity(&cfg));
    assert(ecard_setup_has_wifi());
    assert(!ecard_setup_has_identity());
    assert(!ecard_setup_is_provisioned());
}

static void test_invalid_wifi_and_clear(void) {
    ecard_setup_init();
    assert(!ecard_setup_save_wifi("HomeNet", "short"));
    assert(!ecard_setup_has_wifi());

    assert(ecard_setup_save_wifi("HomeNet", "password123"));
    assert(ecard_setup_clear());
    assert(!ecard_setup_has_wifi());
    assert(!ecard_setup_has_identity());
    assert(!ecard_setup_is_provisioned());
}

static void test_profile_keeps_configured_identity(void) {
    ecard_setup_init();
    assert(ecard_setup_save_wifi("HomeNet", "password123"));

    ecard_setup_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.role = ECARD_ACCOUNT_STUDENT;
    snprintf(cfg.id, sizeof(cfg.id), "20260001");
    snprintf(cfg.school, sizeof(cfg.school), "测试小学");
    snprintf(cfg.class_name, sizeof(cfg.class_name), "二(3)班");
    snprintf(cfg.name, sizeof(cfg.name), "李明");
    snprintf(cfg.phone, sizeof(cfg.phone), "13800138000");
    assert(ecard_setup_save_identity(&cfg));

    // 模拟 main.c 的真实启动顺序：app_init 先重置为出厂档案，
    // 随后 apply_profile 必须把用户配置的身份重新写回去。
    ecard_profile_init_default();
    ecard_setup_apply_profile();

    const ecard_profile_t *profile = ecard_profile_get();
    assert(strcmp(profile->student_name, "李明") == 0);
    assert(strcmp(profile->school_name, "测试小学") == 0);
    assert(strcmp(profile->class_name, "二(3)班") == 0);
    assert(strcmp(profile->student_id, "20260001") == 0);

    // 班级“二(3)班”自动识别当前 2 年级，默认保留 [1, 2, 3] 年级
    uint8_t active[ECARD_MAX_ACTIVE_GRADES] = {0};
    assert(!ecard_setup_has_custom_curriculum());
    assert(ecard_cards_current_grade() == 2);
    assert(ecard_cards_active_grades(active) == 3);
    assert(active[0] == 1 && active[1] == 2 && active[2] == 3);

    // 家长端显式改为 5 年级并仅保留 [4, 5] 两个年级
    const uint8_t custom[2] = {5, 4};
    assert(ecard_setup_save_curriculum(5, custom, 2));
    assert(ecard_setup_has_custom_curriculum());
    ecard_cards_reset_curriculum();
    ecard_setup_apply_profile();
    assert(ecard_cards_current_grade() == 5);
    assert(ecard_cards_active_grades(active) == 2);
    assert(active[0] == 4 && active[1] == 5);

    // 非法超过 3 个年级被拒绝
    const uint8_t too_many[4] = {1, 2, 3, 4};
    assert(!ecard_setup_save_curriculum(3, too_many, 4));
    assert(ecard_cards_current_grade() == 5);

    // clear 后恢复默认 3 年级 [2, 3, 4]
    assert(ecard_setup_clear());
    assert(!ecard_setup_has_custom_curriculum());
    assert(ecard_cards_current_grade() == 3);
}

int main(void) {
    test_two_step_provisioning();
    test_teacher_identity();
    test_invalid_identity_keeps_wifi();
    test_invalid_wifi_and_clear();
    test_profile_keeps_configured_identity();
    puts("test_ecard_setup_state: OK");
    return 0;
}
