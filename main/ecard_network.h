#ifndef ECARD_NETWORK_H
#define ECARD_NETWORK_H

#include "ecard_messages.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 默认预置校园 WiFi 凭据 (若 NVS 为空时回退)
#define ECARD_DEFAULT_CAMPUS_SSID "School_Secure_WiFi"
#define ECARD_DEFAULT_CAMPUS_PASS "Campus@2026#Wifi"
// 演示留言的固定 ID：调用方按 ID 去重，已读后刷新不会再弹回未读。
#define ECARD_DEMO_MESSAGE_ID 1001u

// 局域网自动发现与默认测试中转服务端点
#define ECARD_RELAY_DISCOVER_PORT 8788
#ifndef ECARD_DEFAULT_RELAY_URL
#define ECARD_DEFAULT_RELAY_URL "http://192.168.43.9:8787"
#endif

// 留言发送权限白名单容量
#define ECARD_ACL_MAX_FAMILY 8
#define ECARD_ACL_MAX_TEACHERS 8
#define ECARD_ACL_MAX_ADMINS 4

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

typedef struct {
    char phone[ECARD_MESSAGES_PHONE_MAX];
    char label[24];
} ecard_acl_member_t;

typedef struct {
    char id[20];
    char name[24];
    char phone[ECARD_MESSAGES_PHONE_MAX];
    char school[44];
    char class_name[20];
} ecard_acl_staff_t;

typedef struct {
    char primary_phone[ECARD_MESSAGES_PHONE_MAX];
    char student_school[44];
    char student_class[20];
    ecard_acl_member_t family[ECARD_ACL_MAX_FAMILY];
    uint8_t family_count;
    ecard_acl_staff_t teachers[ECARD_ACL_MAX_TEACHERS];
    uint8_t teacher_count;
    ecard_acl_staff_t admins[ECARD_ACL_MAX_ADMINS];
    uint8_t admin_count;
} ecard_acl_t;

/** 初始化网络子系统与预置凭据/白名单 */
void ecard_network_init(void);

/** 获取当前使用的 WiFi 凭据 */
const ecard_wifi_credentials_t *ecard_network_get_credentials(void);

/** 更新或自定义设置校园 WiFi 凭据 */
void ecard_network_set_credentials(const char *ssid, const char *password);

/** 更新本机留言发送者白名单（主家长 + 家庭成员 + 班级老师 + 学校管理者） */
void ecard_network_set_acl(const ecard_acl_t *acl);

/** 读取当前白名单快照 */
const ecard_acl_t *ecard_network_get_acl(void);

/** 校验某个发送方是否有权限给当前学生发语音留言 */
bool ecard_network_is_sender_authorized(ecard_sender_role_t role,
                                        const char *phone,
                                        const char *school,
                                        const char *class_name);

/** 将协议字符串 ("primary_parent" / "family" / "teacher" / "school_admin") 转为枚举 */
ecard_sender_role_t ecard_network_parse_role(const char *role_str);

/**
 * @brief 判定指定时刻是否处于学校课间时段 (课间才允许自动同步下行留言)
 */
bool ecard_network_is_recess_time(uint8_t hour, uint8_t minute);

/** 启动后台异步同步工作线程（仅 ESP32 生效，绝不阻塞按键与 LVGL） */
void ecard_network_start_sync_worker(void);

/** 异步触发一次下行拉取与待上传补传（非阻塞） */
void ecard_network_request_sync(void);

/** 异步请求上传指定本地留言 ID 的语音文件并回写 ASR 转写文字（非阻塞） */
void ecard_network_request_upload(uint32_t local_msg_id);

/** 异步发送 SOS 报警通知到中转服务（非阻塞） */
void ecard_network_request_sos(const char *student_id, const char *location_desc);

/** 检查并消费同步结果脏标记（供 tick_task 在 LVGL 锁内刷新界面） */
bool ecard_network_consume_sync_dirty(void);

/** 当前生效的中转服务 URL（NVS 配置优先，其次局域网自动发现） */
const char *ecard_network_active_relay_url(void);

/** 最近一次中转同步是否成功在线 */
bool ecard_network_is_relay_online(void);

/**
 * @brief 兼容旧接口：同步检查未读留言（非阻塞环境由后台任务写入 ecard_messages）
 */
bool ecard_network_poll_recess_messages(bool *out_has_new, char *out_sender,
                                        uint8_t *out_duration, uint32_t *out_message_id);

/** 上传学生录制的语音留言（按需触发后台上传） */
bool ecard_network_upload_voice(const uint8_t *adpcm_data, size_t data_len, uint8_t duration_sec);

/** 发送紧急求助 SOS 警报包 */
bool ecard_network_send_sos_alert(const char *student_id, const char *location_desc);

#ifdef __cplusplus
}
#endif

#endif // ECARD_NETWORK_H
