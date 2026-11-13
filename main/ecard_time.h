#ifndef ECARD_TIME_H
#define ECARD_TIME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 启动 SNTP 客户端（北京时间，CST-8）。联网后自动同步。 */
void ecard_time_start_sync(void);

/** 时间是否已与网络对齐。 */
bool ecard_time_is_synced(void);

/**
 * @brief 读取当前时间。
 *
 * @param hour   输出 0..23，可为 NULL
 * @param minute 输出 0..59，可为 NULL
 * @param second 输出 0..59，可为 NULL
 */
void ecard_time_now(uint8_t *hour, uint8_t *minute, uint8_t *second);

#ifdef __cplusplus
}
#endif

#endif // ECARD_TIME_H
