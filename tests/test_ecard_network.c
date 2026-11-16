#include "ecard_network.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_preset_wifi_credentials(void) {
    ecard_network_init();
    const ecard_wifi_credentials_t *creds = ecard_network_get_credentials();
    assert(creds != NULL);
    assert(strcmp(creds->ssid, ECARD_DEFAULT_CAMPUS_SSID) == 0);
    assert(strcmp(creds->password, ECARD_DEFAULT_CAMPUS_PASS) == 0);

    // 修改凭证
    ecard_network_set_credentials("My_Lab_WiFi", "secret666");
    creds = ecard_network_get_credentials();
    assert(strcmp(creds->ssid, "My_Lab_WiFi") == 0);
    assert(strcmp(creds->password, "secret666") == 0);
}

static void test_recess_time_detection(void) {
    // 09:45 处于上午大课间 (09:40 ~ 10:00) -> true
    assert(ecard_network_is_recess_time(9, 45) == true);
    assert(ecard_network_is_recess_time(9, 40) == true);

    // 09:30 处于上课时段 (不上课间) -> false
    assert(ecard_network_is_recess_time(9, 30) == false);

    // 10:00 课间已结束上课 -> false
    assert(ecard_network_is_recess_time(10, 0) == false);

    // 11:50 处于午休放学时段 (11:40 ~ 12:10) -> true
    assert(ecard_network_is_recess_time(11, 50) == true);

    // 15:55 处于下午大课间 (15:40 ~ 16:05) -> true
    assert(ecard_network_is_recess_time(15, 55) == true);

    // 16:20 处于自习/上课时段 -> false
    assert(ecard_network_is_recess_time(16, 20) == false);
}

static void test_network_operations(void) {
    bool has_new = false;
    char sender[32] = {0};
    uint8_t dur = 0;
    uint32_t message_id = 0;

    bool ok = ecard_network_poll_recess_messages(&has_new, sender, &dur, &message_id);
    assert(ok);
    // 校园服务端未接入前，查询成功但没有新留言，不能凭空造未读。
    assert(!has_new);
    assert(message_id == 0);

    uint8_t dummy_adpcm[16] = {0};
    ok = ecard_network_upload_voice(dummy_adpcm, 16, 2);
    assert(ok);

    ok = ecard_network_send_sos_alert("2026110508", "操场");
    assert(ok);
}

static void test_sender_acl_and_roles(void) {
    ecard_network_init();

    assert(ecard_network_parse_role("primary_parent") == ECARD_SENDER_PRIMARY_PARENT);
    assert(ecard_network_parse_role("family") == ECARD_SENDER_FAMILY);
    assert(ecard_network_parse_role("teacher") == ECARD_SENDER_TEACHER);
    assert(ecard_network_parse_role("school_admin") == ECARD_SENDER_SCHOOL_ADMIN);
    assert(ecard_network_parse_role("student") == ECARD_SENDER_STUDENT);
    assert(ecard_network_parse_role("stranger") == ECARD_SENDER_UNKNOWN);

    ecard_acl_t acl;
    memset(&acl, 0, sizeof(acl));
    strcpy(acl.primary_phone, "13800138000");
    strcpy(acl.student_school, "第一小学");
    strcpy(acl.student_class, "三年级二班");
    acl.family_count = 1;
    strcpy(acl.family[0].phone, "13800138001");
    strcpy(acl.family[0].label, "爸爸");
    acl.teacher_count = 1;
    strcpy(acl.teachers[0].id, "T001");
    strcpy(acl.teachers[0].name, "李老师");
    strcpy(acl.teachers[0].phone, "13900001111");
    strcpy(acl.teachers[0].school, "第一小学");
    strcpy(acl.teachers[0].class_name, "三年级二班");
    acl.admin_count = 1;
    strcpy(acl.admins[0].id, "A001");
    strcpy(acl.admins[0].name, "陈主任");
    strcpy(acl.admins[0].phone, "13900002222");
    strcpy(acl.admins[0].school, "第一小学");

    ecard_network_set_acl(&acl);

    // Primary parent & bound family
    assert(ecard_network_is_sender_authorized(ECARD_SENDER_PRIMARY_PARENT, "13800138000", NULL, NULL));
    assert(!ecard_network_is_sender_authorized(ECARD_SENDER_PRIMARY_PARENT, "13899999999", NULL, NULL));
    assert(ecard_network_is_sender_authorized(ECARD_SENDER_FAMILY, "13800138001", NULL, NULL));
    assert(ecard_network_is_sender_authorized(ECARD_SENDER_FAMILY, "13800138000", NULL, NULL));
    assert(!ecard_network_is_sender_authorized(ECARD_SENDER_FAMILY, "13800138099", NULL, NULL));

    // Class teacher matching school & class
    assert(ecard_network_is_sender_authorized(ECARD_SENDER_TEACHER, "13900001111", "第一小学", "三年级二班"));
    assert(!ecard_network_is_sender_authorized(ECARD_SENDER_TEACHER, "13900001111", "第一小学", "三年级一班"));
    assert(!ecard_network_is_sender_authorized(ECARD_SENDER_TEACHER, "13900001111", "第二小学", "三年级二班"));
    assert(!ecard_network_is_sender_authorized(ECARD_SENDER_TEACHER, "13900009999", "第一小学", "三年级二班"));

    // School admin matching school
    assert(ecard_network_is_sender_authorized(ECARD_SENDER_SCHOOL_ADMIN, "13900002222", "第一小学", NULL));
    assert(!ecard_network_is_sender_authorized(ECARD_SENDER_SCHOOL_ADMIN, "13900002222", "第二小学", NULL));
    assert(!ecard_network_is_sender_authorized(ECARD_SENDER_SCHOOL_ADMIN, "13900008888", "第一小学", NULL));

    // Student or unknown cannot push inbound messages
    assert(!ecard_network_is_sender_authorized(ECARD_SENDER_STUDENT, "13800138000", "第一小学", "三年级二班"));
    assert(!ecard_network_is_sender_authorized(ECARD_SENDER_UNKNOWN, "13800138000", "第一小学", "三年级二班"));
}

int main(void) {
    printf("Running ecard_network tests...\n");
    test_preset_wifi_credentials();
    test_recess_time_detection();
    test_network_operations();
    test_sender_acl_and_roles();
    printf("ecard_network tests passed!\n");
    return 0;
}
