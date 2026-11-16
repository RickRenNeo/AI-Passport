#ifndef ECARD_SETUP_VALIDATE_H
#define ECARD_SETUP_VALIDATE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ECARD_SETUP_NAME_MAX 24
#define ECARD_SETUP_SCHOOL_MAX 40
#define ECARD_SETUP_CLASS_MAX 16
#define ECARD_SETUP_ID_DIGITS 8
#define ECARD_SETUP_PHONE_DIGITS 11

typedef enum {
    ECARD_ACCOUNT_STUDENT = 0,
    ECARD_ACCOUNT_TEACHER
} ecard_account_role_t;

/** 8 位教育 ID：必须恰好 8 个数字。 */
bool ecard_setup_valid_education_id(const char *value);

/** 教师工号：1..16 位，只允许数字或字母。 */
bool ecard_setup_valid_staff_id(const char *value);

/** 家长/教师手机号：11 位数字，且以 1 开头。 */
bool ecard_setup_valid_phone(const char *value);

/** 姓名/学校/班级：去除首尾空格后非空，且不超过各自上限。 */
bool ecard_setup_valid_text(const char *value, uint32_t max_len);

/** Wi-Fi SSID 非空、密码 8..64 位。 */
bool ecard_setup_valid_wifi(const char *ssid, const char *password);

/** 学生完整表单校验。 */
bool ecard_setup_valid_student(const char *education_id, const char *school,
                               const char *class_name, const char *name,
                               const char *phone);

/** 教师完整表单校验。 */
bool ecard_setup_valid_teacher(const char *staff_id, const char *school,
                               const char *name, const char *phone);

#ifdef __cplusplus
}
#endif

#endif // ECARD_SETUP_VALIDATE_H
