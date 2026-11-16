#ifndef ECARD_SETUP_H
#define ECARD_SETUP_H

#include "ecard_setup_validate.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ECARD_DEFAULT_WIFI_SSID ""
#define ECARD_DEFAULT_WIFI_PASS ""

typedef struct {
    ecard_account_role_t role;
    char id[20];                 // 学生：8 位教育 ID；教师：工号
    char school[ECARD_SETUP_SCHOOL_MAX + 4];
    char class_name[ECARD_SETUP_CLASS_MAX + 4];
    char name[ECARD_SETUP_NAME_MAX + 4];
    char phone[16];
    bool has_wifi;
    char wifi_ssid[36];
    char wifi_pass[68];
} ecard_setup_t;

/** 打开 NVS 并读取已保存的配置。 */
void ecard_setup_init(void);

/** 是否已保存 Wi-Fi 凭据（不要求身份信息）。 */
bool ecard_setup_has_wifi(void);

/** 是否已保存完整身份信息（不要求 Wi-Fi）。 */
bool ecard_setup_has_identity(void);

/** 是否已完成首次配置（Wi-Fi + 身份）。 */
bool ecard_setup_is_provisioned(void);

/** 当前配置（只读）。 */
const ecard_setup_t *ecard_setup_get(void);

#define ECARD_RELAY_URL_MAX 128

/** 保存身份信息并做完整校验；成功返回 true。 */
bool ecard_setup_save_identity(const ecard_setup_t *identity);

/** 保存 Wi-Fi 凭据（SSID 非空、密码 8..64 位）。 */
bool ecard_setup_save_wifi(const char *ssid, const char *password);

/** 获取已保存的中转服务基地址（未配置时返回空串）。 */
const char *ecard_setup_get_relay_url(void);

/** 保存中转服务基地址（须以 http:// 或 https:// 开头，传空串清空）。 */
bool ecard_setup_save_relay_url(const char *url);

/** 清除全部配置（设备重置）。 */
bool ecard_setup_clear(void);

/** 把已保存的身份写入 eCard 档案，供首页/证件页显示。 */
void ecard_setup_apply_profile(void);

#ifdef __cplusplus
}
#endif

#endif // ECARD_SETUP_H
