#ifndef ECARD_NETWORK_H
#define ECARD_NETWORK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 默认预置校园 WiFi 凭证 (若 NVS 为空时回退)
#define ECARD_DEFAULT_CAMPUS_SSID "School_Secure_WiFi"
#define ECARD_DEFAULT_CAMPUS_PASS "Campus@2026#Wifi"

typedef struct {
    char ssid[33];
    char password[65];
    uint8_t channel;
    uint8_t bssid[6];
    bool has_bssid;
} ecard_wifi_credentials_t;

typedef struct {
    uint8_t start_hour;
    uint8_t start_min;
    uint8_t end_hour;
    uint8_t end_min;
} ecard_schedule_period_t;

/**
 * @brief 初始化网络子系统与预置凭证
 */
void ecard_network_init(void);

/**
 * @brief 获取当前使用的 WiFi 凭证
 */
const ecard_wifi_credentials_t *ecard_network_get_credentials(void);

/**
 * @brief 更新或自定义设置校园 WiFi 凭证 (将保存到 NVS)
 */
void ecard_network_set_credentials(const char *ssid, const char *password);

/**
 * @brief 判定指定时刻是否处于学校课间时段 (课间才允许自动同步下行留言)
 *
 * @param hour 当前小时 (0..23)
 * @param minute 当前分钟 (0..59)
 * @return true 处于课间, false 处于上课/自习时段
 */
bool ecard_network_is_recess_time(uint8_t hour, uint8_t minute);

/**
 * @brief 执行一次按需极速检查未读留言 (上课不触发，课间时段由定时器触发)
 *
 * @param out_has_new 是否获取到新未读留言
 * @param out_sender 留言发送者
 * @param out_duration 留言时长 (秒)
 * @return true 检查成功, false 连接超时或网络失败
 */
bool ecard_network_poll_recess_messages(bool *out_has_new, char *out_sender, uint8_t *out_duration);

/**
 * @brief 上传学生录制的语音留言 (按需连网 -> 上报 -> 立即断网)
 *
 * @param adpcm_data ADPCM 压缩音频数据
 * @param data_len 数据长度 (字节)
 * @param duration_sec 语音时长 (秒)
 * @return true 上传成功, false 上传失败
 */
bool ecard_network_upload_voice(const uint8_t *adpcm_data, size_t data_len, uint8_t duration_sec);

/**
 * @brief 发送紧急求助 SOS 警报包 (最高优先级，附带设备ID、时间与现场录音)
 *
 * @param student_id 学号
 * @param location_desc 位置描述
 * @return true 报警发送成功, false 失败
 */
bool ecard_network_send_sos_alert(const char *student_id, const char *location_desc);

#ifdef __cplusplus
}
#endif

#endif // ECARD_NETWORK_H
