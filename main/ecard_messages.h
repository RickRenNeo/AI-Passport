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
/** 发送方名字上限。 */
#define ECARD_MESSAGES_PEER_MAX 32

typedef enum {
    ECARD_MESSAGE_RECEIVED = 0, // 收到的留言
    ECARD_MESSAGE_SENT           // 本机发出的留言
} ecard_message_dir_t;

/** 一条语音留言（只存元数据；音频仍由网络或内置演示语音提供）。 */
typedef struct {
    uint32_t id;                       // 唯一 ID，用于去重与标记已读
    ecard_message_dir_t direction;     // 收 / 发
    char peer[ECARD_MESSAGES_PEER_MAX];// 收到的发送人，发出的接收人
    uint8_t duration_sec;              // 语音时长
    uint64_t created_ms;              // 单调时钟创建时刻，仅模块内部使用
    bool unread;                       // 仅收到的留言有意义
} ecard_message_t;

/**
 * @brief 初始化留言列表，并写入一组演示数据（含一条 24 小时外的旧留言）。
 *
 * 设备没有 RTC，列表用单调时钟按“距现在多久”排序与过滤，因此不依赖 SNTP。
 */
void ecard_messages_init(void);

/** 周期调用（建议 1 秒一次），推进留言年龄。 */
void ecard_messages_tick(uint32_t delta_ms);

/** 当前单调时钟（毫秒），供相对时间标签使用。 */
uint64_t ecard_messages_now_ms(void);

/**
 * @brief 追加一条留言并按时间倒序插入。
 *
 * @param msg     留言内容；id 为 0 时自动分配
 * @param age_sec 该留言“已经是多久以前”，0 表示刚刚
 * @return 写入后的稳定 ID，0 表示失败
 */
uint32_t ecard_messages_add(ecard_message_t *msg, uint32_t age_sec);

/**
 * @brief 过去 24 小时内的第 index 条留言（0 = 最新）。
 *
 * @return NULL 表示越界
 */
const ecard_message_t *ecard_messages_recent(int index);

/** 过去 24 小时内的留言条数。 */
int ecard_messages_recent_count(void);

/** 最新一条未读留言；没有返回 NULL。 */
const ecard_message_t *ecard_messages_latest_unread(void);

/** 过去 24 小时内的未读条数。 */
int ecard_messages_unread_count(void);

/** 把指定留言标记为已读；返回是否发生了变化。 */
bool ecard_messages_mark_read(uint32_t id);

/** 把“5 分钟前”这类相对时间写入 buf。 */
void ecard_messages_age_label(uint64_t created_ms, char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif // ECARD_MESSAGES_H
