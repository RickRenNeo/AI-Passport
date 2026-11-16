#ifndef ECARD_MESSAGES_H
#define ECARD_MESSAGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 留言列表容量：24 小时内最多保留 20 条，超出后丢弃最旧的一条。 */
#define ECARD_MESSAGES_MAX 20
/** 列表统计窗口：过去 24 小时。 */
#define ECARD_MESSAGES_WINDOW_SEC 86400u
/** 1 MB SPIFFS 分区内预留 ~15% GC/元数据余量，音频总上限 820 KB（约 20 条 10 秒留言）。 */
#define ECARD_MESSAGES_MAX_AUDIO_BYTES (820u * 1024u)
/** 发送方名字上限。 */
#define ECARD_MESSAGES_PEER_MAX 32
/** 语音转写文字上限（UTF-8）。 */
#define ECARD_MESSAGES_TEXT_MAX 256
/** 发送方手机号上限。 */
#define ECARD_MESSAGES_PHONE_MAX 16
/** 服务端消息 ID 上限（用于 ACK 确认即删）。 */
#define ECARD_MESSAGES_REMOTE_ID_MAX 32
/** 本地音频文件路径上限。 */
#define ECARD_MESSAGES_PATH_MAX 64

typedef enum {
    ECARD_MESSAGE_RECEIVED = 0, // 收到的留言
    ECARD_MESSAGE_SENT          // 本机发出的留言
} ecard_message_dir_t;

typedef enum {
    ECARD_SENDER_UNKNOWN = 0,
    ECARD_SENDER_PRIMARY_PARENT, // 设备初始化绑定手机号的主要家长
    ECARD_SENDER_FAMILY,         // 主家长绑定的其他家庭成员
    ECARD_SENDER_TEACHER,        // 本班级老师
    ECARD_SENDER_SCHOOL_ADMIN,   // 学校管理者
    ECARD_SENDER_STUDENT         // 本机学生本人发出
} ecard_sender_role_t;

/** 一条语音留言（含角色、转写文本、时间与本地 SPIFFS 音频索引）。 */
typedef struct {
    uint32_t id;                                   // 本地唯一 ID，用于去重、文件名与标记已读
    ecard_message_dir_t direction;                 // 收 / 发
    ecard_sender_role_t sender_role;               // 发送方角色
    char peer[ECARD_MESSAGES_PEER_MAX];            // 收到的发送人 / 发出的接收人
    char sender_phone[ECARD_MESSAGES_PHONE_MAX];   // 发送方手机号（用于权限校验与来源区分）
    char text[ECARD_MESSAGES_TEXT_MAX];            // 语音转写文字，播放时同步上屏
    char remote_id[ECARD_MESSAGES_REMOTE_ID_MAX];  // 服务端唯一 ID，ACK 后服务端即刻销毁
    uint8_t duration_sec;                          // 语音时长（秒）
    uint32_t audio_bytes;                          // 本地存储的 ADPCM 字节数
    uint64_t created_ms;                           // 单调时钟创建时刻（仅模块内部与相对年龄使用）
    int64_t created_epoch_sec;                     // Unix 秒级时间戳（0 表示尚未校准）
    bool unread;                                   // 仅收到的留言有意义
    bool has_audio;                                // 本地是否已保存真实 ADPCM 音频文件
    bool audio_is_8k;                              // true=8kHz ADPCM, false=16kHz ADPCM
    bool pending_upload;                           // 本地发出但尚未上传完成
} ecard_message_t;

typedef struct {
    bool storage_mounted;
    uint16_t message_count;
    uint16_t max_messages;
    uint32_t total_audio_bytes;
    uint32_t max_audio_bytes;
    uint32_t fs_total_bytes;
    uint32_t fs_used_bytes;
} ecard_messages_storage_stats_t;

/**
 * @brief 初始化留言列表。
 *
 * 未挂载持久化目录时（如宿主单测），写入一组演示数据（含一条 24 小时外的旧留言）。
 * 已挂载持久化目录时（如设备 SPIFFS `/spiffs`），从索引文件恢复并立即按 24 小时窗口裁剪。
 */
void ecard_messages_init(void);

/**
 * @brief 挂载持久化目录（host 测试可传入临时目录，ESP32 传入 `/spiffs`）。
 *
 * 传 NULL 可切回纯内存模式（同时清除已挂载状态）。
 */
bool ecard_messages_mount_dir(const char *dir);

/** ESP32 挂载 `/spiffs` 分区并加载已保存的 24 小时留言；host 平台返回 false。 */
bool ecard_messages_mount_spiffs(void);

/** 清空全部本地留言与音频文件（用于设备重置）。 */
bool ecard_messages_clear_all(void);

/** 周期调用（建议 1 秒一次），推进留言年龄并清理超过 24 小时的过期留言与音频文件。 */
void ecard_messages_tick(uint32_t delta_ms);

/** 校准真实 Unix 时间戳（秒），回填离线留言时间戳并立即裁剪过期留言。 */
void ecard_messages_sync_epoch(int64_t now_epoch_sec);

/** 当前是否已校准真实 Unix 时间戳。 */
bool ecard_messages_epoch_synced(void);

/** 当前单调时钟（毫秒），供相对时间标签使用。 */
uint64_t ecard_messages_now_ms(void);

/**
 * @brief 追加一条留言并按时间倒序插入（不写音频文件）。
 *
 * @param msg     留言内容；id 为 0 时自动分配
 * @param age_sec 该留言“已经是多久以前”，0 表示刚刚
 * @return 写入后的稳定 ID，0 表示失败或已超过 24 小时
 */
uint32_t ecard_messages_add(ecard_message_t *msg, uint32_t age_sec);

/**
 * @brief 追加一条带真实 ADPCM 音频的留言并持久化到存储目录（自动清理超限/过期旧留言）。
 *
 * @param msg        留言元数据（若已填 remote_id 且重复则会去重返回已有 ID）
 * @param age_sec    相对年龄（当 created_epoch_sec==0 时使用）
 * @param adpcm_data ADPCM 音频数据（可为 NULL，配合 prewritten_path 使用）
 * @param adpcm_len  音频字节数
 * @return 本地稳定 ID，0 表示失败/去重跳过/过期
 */
uint32_t ecard_messages_add_with_audio(ecard_message_t *msg, uint32_t age_sec,
                                       const uint8_t *adpcm_data, size_t adpcm_len);

/**
 * @brief 认领由外部流式下载写入的临时音频文件，生成对应 id 并重命名为正式 `.adp` 文件。
 *
 * 适合 ESP32 HTTP 流式下载直接落盘后再入库，全程无需整段音频进堆。
 */
uint32_t ecard_messages_adopt_audio_file(ecard_message_t *msg, uint32_t age_sec,
                                         const char *temp_audio_path, size_t adpcm_len);

/** 检查某条服务端 remote_id 是否已经在本地持久化。 */
bool ecard_messages_has_remote_id(const char *remote_id);

/** 标记本地发出的留言已成功上传到中转服务，并回写服务端分配的 remote_id 与 ASR 转写文字。 */
bool ecard_messages_mark_uploaded(uint32_t local_id, const char *remote_id, const char *asr_text);

/** 读取一条待上传的本机留言副本；无待上传返回 false。 */
bool ecard_messages_next_pending_upload(ecard_message_t *out);

/** 生成指定留言 ID 对应的本地音频文件路径（例如 `/spiffs/msg_1.adp`）。 */
bool ecard_messages_audio_path(uint32_t id, char *buf, size_t size);

/** 为外部流式下载生成临时音频文件路径（例如 `/spiffs/tmp_dl.adp`）。 */
bool ecard_messages_temp_audio_path(char *buf, size_t size);

/** 为本机录音落盘生成独立临时路径（例如 `/spiffs/tmp_rec.adp`），避免与下载临时文件冲突。 */
bool ecard_messages_rec_temp_audio_path(char *buf, size_t size);

/** 过去 24 小时内的第 index 条留言（0 = 最新），NULL 表示越界。 */
const ecard_message_t *ecard_messages_recent(int index);

/** 线程安全读取过去 24 小时内的第 index 条留言副本。 */
bool ecard_messages_copy_recent(int index, ecard_message_t *out);

/** 线程安全按 ID 读取留言副本。 */
bool ecard_messages_copy_by_id(uint32_t id, ecard_message_t *out);

/** 过去 24 小时内的留言条数。 */
int ecard_messages_recent_count(void);

/** 最新一条未读留言；没有返回 NULL。 */
const ecard_message_t *ecard_messages_latest_unread(void);

/** 线程安全读取最新一条未读留言副本。 */
bool ecard_messages_copy_latest_unread(ecard_message_t *out);

/** 过去 24 小时内的未读条数。 */
int ecard_messages_unread_count(void);

/** 把指定留言标记为已读；返回是否发生了变化（若已挂载存储则持久化）。 */
bool ecard_messages_mark_read(uint32_t id);

/** 把“5 分钟前”这类相对时间写入 buf。 */
void ecard_messages_age_label(uint64_t created_ms, char *buf, size_t size);

/** 返回角色短标签（如“家长”“家人”“老师”“校管”“我”）。 */
const char *ecard_messages_role_label(ecard_sender_role_t role);

/** 返回角色单字徽章（如“家”“师”“校”“我”）。 */
const char *ecard_messages_role_badge(ecard_sender_role_t role);

/** 获取当前持久化存储占用统计。 */
void ecard_messages_get_storage_stats(ecard_messages_storage_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif // ECARD_MESSAGES_H
