#ifndef ECARD_PORTAL_H
#define ECARD_PORTAL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 打开手机配网页（SoftAP + HTTP 表单）。
 *
 * 用于首次配置 Wi-Fi 与身份，或默认 Wi-Fi 不可用时的回退。
 * 保存成功后设备自动重启。
 *
 * @return true 表示门户已启动
 */
bool ecard_portal_start(void);

/** @brief 关闭配网热点与 Web 服务，释放 SoftAP 与 HTTP/DNS 内存。 */
void ecard_portal_stop(void);

/** 门户是否正在运行。 */
bool ecard_portal_is_active(void);

/** 热点名称（如 AI-Passport-1A2B）。 */
const char *ecard_portal_ssid(void);

/** 热点密码。 */
const char *ecard_portal_password(void);

#ifdef __cplusplus
}
#endif

#endif // ECARD_PORTAL_H
