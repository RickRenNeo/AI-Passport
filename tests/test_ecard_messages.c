#include "ecard_messages.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_seed_order_and_window(void) {
    ecard_messages_init();

    // 演示数据 6 条，其中 1 条是 26 小时前，24 小时窗口内应只剩 5 条。
    assert(ecard_messages_recent_count() == 5);
    assert(ecard_messages_recent(5) == NULL);

    const ecard_message_t *newest = ecard_messages_recent(0);
    assert(newest != NULL);
    assert(strcmp(newest->peer, "妈妈") == 0);
    assert(newest->direction == ECARD_MESSAGE_RECEIVED);
    assert(newest->unread);

    // 时间倒序：越往后越旧
    for (int i = 1; i < 5; ++i) {
        const ecard_message_t *a = ecard_messages_recent(i - 1);
        const ecard_message_t *b = ecard_messages_recent(i);
        assert(a && b);
        assert(a->created_ms >= b->created_ms);
    }

    // 发出的留言天然已读
    const ecard_message_t *sent = ecard_messages_recent(1);
    assert(sent && sent->direction == ECARD_MESSAGE_SENT);
    assert(!sent->unread);
}

static void test_unread_and_mark_read(void) {
    ecard_messages_init();
    assert(ecard_messages_unread_count() == 2);

    const ecard_message_t *latest = ecard_messages_latest_unread();
    assert(latest && strcmp(latest->peer, "妈妈") == 0);

    assert(ecard_messages_mark_read(latest->id));
    assert(ecard_messages_unread_count() == 1);
    // 同一条重复标记不再产生变化
    assert(!ecard_messages_mark_read(latest->id));
    assert(ecard_messages_unread_count() == 1);
}

static void test_dedupe_by_id(void) {
    ecard_messages_init();

    ecard_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.id = 1001;
    msg.direction = ECARD_MESSAGE_RECEIVED;
    msg.duration_sec = 7;
    msg.unread = true;
    strcpy(msg.peer, "妈妈");
    assert(ecard_messages_add(&msg, 0) == 1001);

    const int before = ecard_messages_recent_count();
    assert(ecard_messages_unread_count() == 3);

    ecard_message_t dup = msg;
    assert(ecard_messages_add(&dup, 0) == 1001);
    assert(ecard_messages_recent_count() == before);
    assert(ecard_messages_unread_count() == 3);

    // 读过之后再轮询到同一 ID，不能复活未读
    assert(ecard_messages_mark_read(1001));
    assert(ecard_messages_unread_count() == 2);
    ecard_messages_add(&dup, 0);
    assert(ecard_messages_unread_count() == 2);
}

static void test_window_expires_with_time(void) {
    ecard_messages_init();

    // 再过 14 小时：24 小时窗口里的 5 条仍然都在
    ecard_messages_tick(14u * 3600u * 1000u);
    assert(ecard_messages_recent_count() == 5);

    // 再推进 2 小时：最旧的 9 小时前那条变成 25 小时前，被移出列表
    ecard_messages_tick(2u * 3600u * 1000u);
    assert(ecard_messages_recent_count() == 4);
}

static void test_capacity_keeps_newest(void) {
    ecard_messages_init();

    ecard_message_t msg;
    for (uint32_t i = 0; i < 30; ++i) {
        memset(&msg, 0, sizeof(msg));
        msg.direction = ECARD_MESSAGE_RECEIVED;
        msg.duration_sec = (uint8_t)(i % 15 + 1);
        strcpy(msg.peer, "同学");
        ecard_messages_add(&msg, 0);
    }

    assert(ecard_messages_recent_count() <= ECARD_MESSAGES_MAX);
    const ecard_message_t *newest = ecard_messages_recent(0);
    assert(newest != NULL);
    // 最新插入的那条必须在最前面
    assert(newest->duration_sec == (uint8_t)(29 % 15 + 1));
}

static void test_age_labels(void) {
    ecard_messages_init();
    char buf[24];

    ecard_messages_age_label(ecard_messages_now_ms(), buf, sizeof(buf));
    assert(strcmp(buf, "刚刚") == 0);

    ecard_messages_age_label(ecard_messages_now_ms() - 5u * 60u * 1000u, buf, sizeof(buf));
    assert(strcmp(buf, "5 分钟前") == 0);

    ecard_messages_age_label(ecard_messages_now_ms() - 3u * 3600u * 1000u, buf, sizeof(buf));
    assert(strcmp(buf, "3 小时前") == 0);
}

int main(void) {
    printf("Running ecard_messages tests...\n");
    test_seed_order_and_window();
    test_unread_and_mark_read();
    test_dedupe_by_id();
    test_window_expires_with_time();
    test_capacity_keeps_newest();
    test_age_labels();
    printf("ecard_messages tests passed!\n");
    return 0;
}
