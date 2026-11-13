#include "ecard_messages.h"
#include <stdio.h>
#include <string.h>

#define WINDOW_MS ((uint64_t)ECARD_MESSAGES_WINDOW_SEC * 1000ull)
// 演示数据可以带“26 小时前”这种超出窗口的条目，因此时钟从 2 天开始。
#define CLOCK_START_MS (2ull * WINDOW_MS)

static ecard_message_t s_messages[ECARD_MESSAGES_MAX];
static int s_count;
static uint64_t s_now_ms = CLOCK_START_MS;
static uint32_t s_next_id = 1;

static uint32_t age_sec_of(const ecard_message_t *msg) {
    if (msg->created_ms >= s_now_ms) return 0;
    return (uint32_t)((s_now_ms - msg->created_ms) / 1000ull);
}

static void insert_sorted(ecard_message_t msg) {
    if (s_count < ECARD_MESSAGES_MAX) {
        ++s_count;
    } else {
        // 满了：先丢掉最旧的一条，再插到前面。
        s_count = ECARD_MESSAGES_MAX - 1;
    }

    int pos = s_count;
    // 同一毫秒内追加多条时，后插入的视为更新，排在前面。
    while (pos > 0 && s_messages[pos - 1].created_ms <= msg.created_ms) {
        s_messages[pos] = s_messages[pos - 1];
        --pos;
    }
    s_messages[pos] = msg;
}

void ecard_messages_init(void) {
    memset(s_messages, 0, sizeof(s_messages));
    s_count = 0;
    s_now_ms = CLOCK_START_MS;
    s_next_id = 1;

    // 演示数据：有未读、有已读、有发出的，也有一条 26 小时前的旧留言，
    // 用来验证列表确实按 24 小时窗口过滤。
    ecard_message_t demo[] = {
        { .direction = ECARD_MESSAGE_RECEIVED, .peer = "妈妈", .duration_sec = 7,
          .unread = true },
        { .direction = ECARD_MESSAGE_SENT, .peer = "妈妈", .duration_sec = 6 },
        { .direction = ECARD_MESSAGE_RECEIVED, .peer = "王老师", .duration_sec = 9,
          .unread = true },
        { .direction = ECARD_MESSAGE_RECEIVED, .peer = "爸爸", .duration_sec = 12 },
        { .direction = ECARD_MESSAGE_SENT, .peer = "爸爸", .duration_sec = 6 },
        { .direction = ECARD_MESSAGE_RECEIVED, .peer = "妈妈", .duration_sec = 7 },
    };
    static const uint32_t demo_age_sec[] = { 180u, 1680u, 7200u, 18000u, 32400u,
                                             93600u };

    for (size_t i = 0; i < sizeof(demo) / sizeof(demo[0]); ++i) {
        ecard_messages_add(&demo[i], demo_age_sec[i]);
    }
}

void ecard_messages_tick(uint32_t delta_ms) {
    s_now_ms += delta_ms;
}

uint64_t ecard_messages_now_ms(void) {
    return s_now_ms;
}

uint32_t ecard_messages_add(ecard_message_t *msg, uint32_t age_sec) {
    if (!msg) return 0;

    if (msg->id == 0) {
        msg->id = s_next_id++;
        if (s_next_id == 0) s_next_id = 1;
    }

    // 同一 ID 不重复插入：网络轮询可能多次返回同一条留言。
    for (int i = 0; i < s_count; ++i) {
        if (s_messages[i].id == msg->id) {
            return s_messages[i].id;
        }
    }

    const uint64_t age_ms = (uint64_t)age_sec * 1000ull;
    ecard_message_t stored = *msg;
    stored.created_ms = (age_ms >= s_now_ms) ? 0ull : s_now_ms - age_ms;
    if (stored.direction == ECARD_MESSAGE_SENT) stored.unread = false;
    insert_sorted(stored);
    return stored.id;
}

const ecard_message_t *ecard_messages_recent(int index) {
    if (index < 0) return NULL;

    int seen = 0;
    for (int i = 0; i < s_count; ++i) {
        if (age_sec_of(&s_messages[i]) > ECARD_MESSAGES_WINDOW_SEC) break;
        if (seen == index) return &s_messages[i];
        ++seen;
    }
    return NULL;
}

int ecard_messages_recent_count(void) {
    int seen = 0;
    for (int i = 0; i < s_count; ++i) {
        if (age_sec_of(&s_messages[i]) > ECARD_MESSAGES_WINDOW_SEC) break;
        ++seen;
    }
    return seen;
}

const ecard_message_t *ecard_messages_latest_unread(void) {
    for (int i = 0; i < s_count; ++i) {
        if (age_sec_of(&s_messages[i]) > ECARD_MESSAGES_WINDOW_SEC) break;
        if (s_messages[i].direction == ECARD_MESSAGE_RECEIVED && s_messages[i].unread) {
            return &s_messages[i];
        }
    }
    return NULL;
}

int ecard_messages_unread_count(void) {
    int seen = 0;
    for (int i = 0; i < s_count; ++i) {
        if (age_sec_of(&s_messages[i]) > ECARD_MESSAGES_WINDOW_SEC) break;
        if (s_messages[i].direction == ECARD_MESSAGE_RECEIVED && s_messages[i].unread) {
            ++seen;
        }
    }
    return seen;
}

bool ecard_messages_mark_read(uint32_t id) {
    for (int i = 0; i < s_count; ++i) {
        if (s_messages[i].id != id) continue;
        if (!s_messages[i].unread) return false;
        s_messages[i].unread = false;
        return true;
    }
    return false;
}

void ecard_messages_age_label(uint64_t created_ms, char *buf, size_t size) {
    if (!buf || size == 0) return;
    buf[0] = '\0';

    const uint32_t age = (created_ms >= s_now_ms) ? 0
                                                 : (uint32_t)((s_now_ms - created_ms) / 1000ull);
    if (age < 60) {
        snprintf(buf, size, "刚刚");
    } else if (age < 3600) {
        snprintf(buf, size, "%u 分钟前", (unsigned)(age / 60));
    } else if (age < ECARD_MESSAGES_WINDOW_SEC) {
        snprintf(buf, size, "%u 小时前", (unsigned)(age / 3600));
    } else {
        snprintf(buf, size, "%u 天前", (unsigned)(age / ECARD_MESSAGES_WINDOW_SEC));
    }
}
