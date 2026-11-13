#include "ecard_setup_validate.h"
#include <ctype.h>
#include <string.h>

static bool is_digits(const char *value, int count) {
    if (!value) return false;
    int len = 0;
    while (value[len] != '\0') {
        if (!isdigit((unsigned char)value[len])) return false;
        ++len;
    }
    return len == count;
}

bool ecard_setup_valid_education_id(const char *value) {
    return is_digits(value, ECARD_SETUP_ID_DIGITS);
}

bool ecard_setup_valid_staff_id(const char *value) {
    if (!value) return false;
    int len = 0;
    for (const char *p = value; *p; ++p) {
        if (!isalnum((unsigned char)*p)) return false;
        if (++len > 16) return false;
    }
    return len >= 1;
}

bool ecard_setup_valid_phone(const char *value) {
    if (!is_digits(value, ECARD_SETUP_PHONE_DIGITS)) return false;
    return value[0] == '1';
}

bool ecard_setup_valid_text(const char *value, uint32_t max_len) {
    if (!value || max_len == 0) return false;
    uint32_t len = 0;
    // 纯空白视为空
    bool has_content = false;
    for (const char *p = value; *p; ++p) {
        if (!isspace((unsigned char)*p)) {
            has_content = true;
            break;
        }
    }
    if (!has_content) return false;
    for (const char *p = value; *p; ++p) {
        if (++len > max_len) return false;
    }
    return true;
}

bool ecard_setup_valid_wifi(const char *ssid, const char *password) {
    if (!ecard_setup_valid_text(ssid, 32)) return false;
    if (!password) return false;
    const size_t len = strlen(password);
    return len >= 8 && len <= 64;
}

bool ecard_setup_valid_student(const char *education_id, const char *school,
                               const char *class_name, const char *name,
                               const char *phone) {
    return ecard_setup_valid_education_id(education_id) &&
           ecard_setup_valid_text(school, ECARD_SETUP_SCHOOL_MAX) &&
           ecard_setup_valid_text(class_name, ECARD_SETUP_CLASS_MAX) &&
           ecard_setup_valid_text(name, ECARD_SETUP_NAME_MAX) &&
           ecard_setup_valid_phone(phone);
}

bool ecard_setup_valid_teacher(const char *staff_id, const char *school,
                               const char *name, const char *phone) {
    return ecard_setup_valid_staff_id(staff_id) &&
           ecard_setup_valid_text(school, ECARD_SETUP_SCHOOL_MAX) &&
           ecard_setup_valid_text(name, ECARD_SETUP_NAME_MAX) &&
           ecard_setup_valid_phone(phone);
}
