#include "ecard_messages.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void make_temp_dir(char *buf, size_t size) {
    snprintf(buf, size, "/tmp/ecard_msg_test_%d", (int)getpid());
    char *res = mkdtemp(buf);
    assert(res != NULL);
}

static void remove_temp_dir(const char *dir) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    (void)system(cmd);
}

static void test_seed_order_and_window(void) {
    ecard_messages_mount_dir(NULL);
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
    ecard_messages_mount_dir(NULL);
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
    ecard_messages_mount_dir(NULL);
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
    ecard_messages_mount_dir(NULL);
    ecard_messages_init();

    // 再过 14 小时：24 小时窗口里的 5 条仍然都在
    ecard_messages_tick(14u * 3600u * 1000u);
    assert(ecard_messages_recent_count() == 5);

    // 再推进 2 小时：最旧的 9 小时前那条变成 25 小时前，被移出列表
    ecard_messages_tick(2u * 3600u * 1000u);
    assert(ecard_messages_recent_count() == 4);
}

static void test_capacity_keeps_newest(void) {
    ecard_messages_mount_dir(NULL);
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
    ecard_messages_mount_dir(NULL);
    ecard_messages_init();
    char buf[24];

    ecard_messages_age_label(ecard_messages_now_ms(), buf, sizeof(buf));
    assert(strcmp(buf, "刚刚") == 0);

    ecard_messages_age_label(ecard_messages_now_ms() - 5u * 60u * 1000u, buf, sizeof(buf));
    assert(strcmp(buf, "5 分钟前") == 0);

    ecard_messages_age_label(ecard_messages_now_ms() - 3u * 3600u * 1000u, buf, sizeof(buf));
    assert(strcmp(buf, "3 小时前") == 0);
}

static void test_persistence_reboot_and_upload(void) {
    char dir[128];
    make_temp_dir(dir, sizeof(dir));

    assert(ecard_messages_mount_dir(dir));
    assert(ecard_messages_recent_count() == 0);

    const int64_t now_ep = 1760000000LL;
    ecard_messages_sync_epoch(now_ep);
    assert(ecard_messages_epoch_synced());

    uint8_t audio1[4000];
    memset(audio1, 0x5A, sizeof(audio1));
    ecard_message_t m1;
    memset(&m1, 0, sizeof(m1));
    m1.direction = ECARD_MESSAGE_RECEIVED;
    m1.sender_role = ECARD_SENDER_TEACHER;
    m1.duration_sec = 5;
    m1.unread = true;
    m1.audio_is_8k = true;
    m1.created_epoch_sec = now_ep - 60;
    strcpy(m1.peer, "李老师");
    strcpy(m1.sender_phone, "13900001111");
    strcpy(m1.text, "记得交数学作业");
    strcpy(m1.remote_id, "srv_msg_1");
    uint32_t id1 = ecard_messages_add_with_audio(&m1, 60, audio1, sizeof(audio1));
    assert(id1 != 0);
    assert(ecard_messages_has_remote_id("srv_msg_1"));
    // Duplicate remote_id should be ignored cleanly
    assert(ecard_messages_add_with_audio(&m1, 60, audio1, sizeof(audio1)) == id1);
    assert(ecard_messages_recent_count() == 1);

    // Adopt a prewritten temp file
    char tmp_path[128];
    assert(ecard_messages_temp_audio_path(tmp_path, sizeof(tmp_path)));
    FILE *fp = fopen(tmp_path, "wb");
    assert(fp != NULL);
    uint8_t audio2[2000];
    memset(audio2, 0x33, sizeof(audio2));
    assert(fwrite(audio2, 1, sizeof(audio2), fp) == sizeof(audio2));
    fclose(fp);

    ecard_message_t m2;
    memset(&m2, 0, sizeof(m2));
    m2.direction = ECARD_MESSAGE_SENT;
    m2.sender_role = ECARD_SENDER_STUDENT;
    m2.duration_sec = 2;
    m2.audio_is_8k = true;
    m2.pending_upload = true;
    strcpy(m2.peer, "家长");
    uint32_t id2 = ecard_messages_adopt_audio_file(&m2, 0, tmp_path, sizeof(audio2));
    assert(id2 != 0 && id2 != id1);

    ecard_message_t pending;
    assert(ecard_messages_next_pending_upload(&pending));
    assert(pending.id == id2);
    assert(ecard_messages_mark_uploaded(id2, "srv_up_2", "妈妈我放学了"));
    assert(!ecard_messages_next_pending_upload(&pending));
    assert(ecard_messages_mark_read(id1));

    // Simulate reboot: remount same directory
    assert(ecard_messages_mount_dir(NULL));
    assert(ecard_messages_mount_dir(dir));
    assert(ecard_messages_recent_count() == 2);
    assert(ecard_messages_unread_count() == 0);

    ecard_message_t restored;
    assert(ecard_messages_copy_by_id(id1, &restored));
    assert(restored.sender_role == ECARD_SENDER_TEACHER);
    assert(strcmp(restored.text, "记得交数学作业") == 0);
    assert(restored.has_audio && restored.audio_bytes == sizeof(audio1));
    char apath[128];
    assert(ecard_messages_audio_path(id1, apath, sizeof(apath)));
    assert(access(apath, F_OK) == 0);

    assert(ecard_messages_copy_by_id(id2, &restored));
    assert(strcmp(restored.text, "妈妈我放学了") == 0);
    assert(!restored.pending_upload);

    // Advance epoch > 24h to verify automatic pruning deletes files
    ecard_messages_sync_epoch(now_ep + (int64_t)ECARD_MESSAGES_WINDOW_SEC + 10);
    assert(ecard_messages_recent_count() == 0);
    assert(access(apath, F_OK) != 0);

    ecard_messages_mount_dir(NULL);
    remove_temp_dir(dir);
}

static void test_byte_cap_eviction_and_clear_all(void) {
    char dir[128];
    make_temp_dir(dir, sizeof(dir));
    assert(ecard_messages_mount_dir(dir));

    // Each message 100 KB -> adding 10 exceeds 820 KB cap -> keeps at most 8 (800 KB)
    const size_t chunk_sz = 100u * 1024u;
    uint8_t *buf = malloc(chunk_sz);
    assert(buf != NULL);
    memset(buf, 0x22, chunk_sz);

    uint32_t first_id = 0;
    for (int i = 0; i < 10; ++i) {
        ecard_message_t m;
        memset(&m, 0, sizeof(m));
        m.direction = ECARD_MESSAGE_RECEIVED;
        m.sender_role = ECARD_SENDER_FAMILY;
        m.duration_sec = 10;
        snprintf(m.peer, sizeof(m.peer), "家人%d", i);
        uint32_t id = ecard_messages_add_with_audio(&m, 0, buf, chunk_sz);
        assert(id != 0);
        if (i == 0) first_id = id;
    }
    free(buf);

    ecard_messages_storage_stats_t stats;
    ecard_messages_get_storage_stats(&stats);
    assert(stats.storage_mounted);
    assert(stats.total_audio_bytes <= ECARD_MESSAGES_MAX_AUDIO_BYTES);
    assert(stats.message_count == 8);

    char evicted_path[128];
    assert(ecard_messages_audio_path(first_id, evicted_path, sizeof(evicted_path)));
    assert(access(evicted_path, F_OK) != 0);

    assert(ecard_messages_clear_all());
    ecard_messages_get_storage_stats(&stats);
    assert(stats.message_count == 0);
    assert(stats.total_audio_bytes == 0);

    // 验证本机录音临时路径与下载临时路径彼此隔离，且 clear_all 会一并清理残留
    char dl_tmp[128];
    char rec_tmp[128];
    assert(ecard_messages_temp_audio_path(dl_tmp, sizeof(dl_tmp)));
    assert(ecard_messages_rec_temp_audio_path(rec_tmp, sizeof(rec_tmp)));
    assert(strcmp(dl_tmp, rec_tmp) != 0);
    FILE *rec_fp = fopen(rec_tmp, "wb");
    assert(rec_fp != NULL);
    assert(fwrite("ADPCM", 1, 5, rec_fp) == 5);
    fclose(rec_fp);
    assert(access(rec_tmp, F_OK) == 0);
    assert(ecard_messages_clear_all());
    assert(access(rec_tmp, F_OK) != 0);

    ecard_messages_mount_dir(NULL);
    assert(!ecard_messages_rec_temp_audio_path(rec_tmp, sizeof(rec_tmp)));
    remove_temp_dir(dir);
}

int main(void) {
    printf("Running ecard_messages tests...\n");
    test_seed_order_and_window();
    test_unread_and_mark_read();
    test_dedupe_by_id();
    test_window_expires_with_time();
    test_capacity_keeps_newest();
    test_age_labels();
    test_persistence_reboot_and_upload();
    test_byte_cap_eviction_and_clear_all();
    printf("ecard_messages tests passed!\n");
    return 0;
}
