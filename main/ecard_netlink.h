#ifndef ECARD_NETLINK_H
#define ECARD_NETLINK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 稳定态重试间隔：连续失败后每 2 分钟一次。 */
#define ECARD_NETLINK_RETRY_MS (120u * 1000u)
/** 刚断开/刚开机时的快速重试间隔：5s、10s、20s、30s。 */
#define ECARD_NETLINK_FAST_RETRY_MS (5u * 1000u)
/** 连续失败次数达到后转入手机配网门户。 */
#define ECARD_NETLINK_FAILS_BEFORE_PORTAL 3

/** 初始化 Wi-Fi（STA）与时间同步钩子；失败时返回 false。 */
bool ecard_netlink_init(void);

/** 周期调用（建议 1 秒一次）：负责重连、失败计数与配网回退。 */
void ecard_netlink_tick(void);

/** 当前是否已联网。 */
bool ecard_netlink_is_connected(void);

/** 连续失败次数；配网页用它提示“密码错误/仍在连接”。 */
uint8_t ecard_netlink_fail_count(void);

/** 当前使用的 SSID；未连接返回空串。 */
const char *ecard_netlink_ssid(void);

/** 立即触发一次重连（配网保存后调用）。 */
void ecard_netlink_reconnect(void);

/**
 * @brief AI 语音会话期间关闭 Wi-Fi 省电，避免长 TTS 被 DTIM 拖慢/断流。
 *
 * 会话结束必须传 false 恢复 WIFI_PS_MIN_MODEM，兼顾电池续航。
 */
void ecard_netlink_set_low_latency(bool on);

/**
 * @brief 配网页扫描前调用：暂时断开 STA，扫描不算失败。
 *
 * ESP-IDF 在 STA 正在连接时拒绝扫描，因此扫描必须先断开。
 * 这段断开是设备主动行为，不能计入“密码错误”的失败次数。
 */
void ecard_netlink_scan_begin(void);

/**
 * @brief 配网页扫描结束后调用：恢复 STA 连接。
 */
void ecard_netlink_scan_end(void);

#ifdef __cplusplus
}
#endif

#endif // ECARD_NETLINK_H
