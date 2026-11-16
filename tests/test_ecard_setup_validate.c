#include "ecard_setup_validate.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_education_id(void) {
    assert(ecard_setup_valid_education_id("20260101"));
    assert(!ecard_setup_valid_education_id("2026010"));    // 7 位
    assert(!ecard_setup_valid_education_id("202601011"));  // 9 位
    assert(!ecard_setup_valid_education_id("2026010A"));
    assert(!ecard_setup_valid_education_id(NULL));
    assert(!ecard_setup_valid_education_id(""));
}

static void test_staff_id(void) {
    assert(ecard_setup_valid_staff_id("T1024"));
    assert(ecard_setup_valid_staff_id("1024"));
    assert(!ecard_setup_valid_staff_id(""));
    assert(!ecard_setup_valid_staff_id("T-1024"));   // 含标点
    assert(!ecard_setup_valid_staff_id("12345678901234567")); // 17 位
}

static void test_phone(void) {
    assert(ecard_setup_valid_phone("13812345678"));
    assert(!ecard_setup_valid_phone("23812345678"));  // 不是 1 开头
    assert(!ecard_setup_valid_phone("1381234567"));   // 10 位
    assert(!ecard_setup_valid_phone("138123456789")); // 12 位
    assert(!ecard_setup_valid_phone("1381234567a"));
}

static void test_text(void) {
    assert(ecard_setup_valid_text("张小明", ECARD_SETUP_NAME_MAX));
    assert(!ecard_setup_valid_text("   ", ECARD_SETUP_NAME_MAX));
    assert(!ecard_setup_valid_text("", ECARD_SETUP_NAME_MAX));
    assert(!ecard_setup_valid_text(NULL, ECARD_SETUP_NAME_MAX));
    char too_long[32];
    for (int i = 0; i < 25; ++i) too_long[i] = 'a';
    too_long[25] = '\0';
    assert(!ecard_setup_valid_text(too_long, ECARD_SETUP_NAME_MAX));
}

static void test_wifi(void) {
    assert(ecard_setup_valid_wifi("ExampleWiFi", "password123"));
    assert(!ecard_setup_valid_wifi("", "password123"));
    assert(!ecard_setup_valid_wifi("ExampleWiFi", "1234567"));
    assert(!ecard_setup_valid_wifi("ExampleWiFi", NULL));
}

static void test_forms(void) {
    assert(ecard_setup_valid_student("20260101", "XX市第一实验学校", "一(8)班",
                                     "张小明", "13812345678"));
    assert(!ecard_setup_valid_student("2026010", "XX市第一实验学校", "一(8)班",
                                      "张小明", "13812345678"));
    assert(!ecard_setup_valid_student("20260101", "XX市第一实验学校", "一(8)班",
                                      "张小明", "1381234567"));
    assert(!ecard_setup_valid_student("20260101", "", "一(8)班", "张小明",
                                      "13812345678"));

    assert(ecard_setup_valid_teacher("T1024", "XX市第一实验学校", "王老师",
                                     "13912345678"));
    assert(!ecard_setup_valid_teacher("", "XX市第一实验学校", "王老师",
                                      "13912345678"));
}

int main(void) {
    printf("Running ecard_setup_validate tests...\n");
    test_education_id();
    test_staff_id();
    test_phone();
    test_text();
    test_wifi();
    test_forms();
    printf("ecard_setup_validate tests passed!\n");
    return 0;
}
