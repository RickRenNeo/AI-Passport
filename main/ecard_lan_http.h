#ifndef ECARD_LAN_HTTP_H
#define ECARD_LAN_HTTP_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 STA 局域网配对与小猫书房 HTTP JSON 服务（端口 80）。
 *
 * 仅在配网已完成、配网 AP 门户未开启、STA 已连接且小智 AI 空闲时启动。
 */
bool ecard_lan_http_start(void);

/**
 * @brief 停止 STA 局域网 HTTP 服务并释放缓冲/套接字资源。
 */
void ecard_lan_http_stop(void);

/**
 * @brief STA 局域网 HTTP 服务当前是否正在运行。
 */
bool ecard_lan_http_is_running(void);

/**
 * @brief 通知局域网 HTTP 服务小智 AI 会话占用状态（互斥释放约 40 KB TLS+Opus 堆）。
 *
 * busy=true 时立即停止 HTTP 服务；busy=false 时若 STA 已连网且已配网则自动重启。
 */
void ecard_lan_http_set_ai_busy(bool busy);

/**
 * @brief 获取当前设备 STA IPv4 字符串（如 "192.168.1.42"）。
 */
bool ecard_lan_http_get_ip(char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif // ECARD_LAN_HTTP_H
