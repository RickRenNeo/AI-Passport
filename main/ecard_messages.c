#include "ecard_messages.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include <esp_log.h>
#include <esp_spiffs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#define WINDOW_MS ((uint64_t)ECARD_MESSAGES_WINDOW_SEC * 1000ull)
// 演示数据可以带“26 小时前”这种超出窗口的条目，因此时钟从 2 天开始。
#define CLOCK_START_MS (2ull * WINDOW_MS)
#define INDEX_MAGIC 0x45434D32u // 'ECM2'
#define INDEX_VERSION 1u
#define STORAGE_DIR_MAX 48
#define VALID_EPOCH_MIN 1700000000LL

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint32_t next_id;
} ecard_index_header_t;

static ecard_message_t s_messages[ECARD_MESSAGES_MAX];
static int s_count;
static uint64_t s_now_ms = CLOCK_START_MS;
static uint32_t s_next_id = 1;
static int64_t s_now_epoch = 0;
static bool s_epoch_synced = false;
static bool s_storage_mounted = false;
static char s_storage_dir[STORAGE_DIR_MAX];

#if defined(ESP_PLATFORM)
static const char *TAG = "ecard_msgs";
static SemaphoreHandle_t s_mutex = NULL;
static bool s_spiffs_registered = false;

static void msg_lock(void) {
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (s_mutex) {
        (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void msg_unlock(void) {
    if (s_mutex) {
        (void)xSemaphoreGive(s_mutex);
    }
}
#else
static void msg_lock(void) {}
static void msg_unlock(void) {}
#endif

static uint32_t age_sec_of_locked(const ecard_message_t *msg) {
    if (s_epoch_synced && msg->created_epoch_sec > 0) {
        if (msg->created_epoch_sec >= s_now_epoch) return 0;
        return (uint32_t)(s_now_epoch - msg->created_epoch_sec);
    }
    if (msg->created_ms >= s_now_ms) return 0;
    return (uint32_t)((s_now_ms - msg->created_ms) / 1000ull);
}

static bool build_audio_path_locked(uint32_t id, char *buf, size_t size) {
    if (!s_storage_mounted || id == 0 || !buf || size == 0) return false;
    int n = snprintf(buf, size, "%s/msg_%lu.adp", s_storage_dir, (unsigned long)id);
    return n > 0 && (size_t)n < size;
}

static void delete_audio_file_locked(uint32_t id) {
    char path[80];
    if (build_audio_path_locked(id, path, sizeof(path))) {
        (void)remove(path);
    }
}

static uint32_t total_audio_bytes_locked(void) {
    uint32_t total = 0;
    for (int i = 0; i < s_count; ++i) {
        if (s_messages[i].has_audio) {
            total += s_messages[i].audio_bytes;
        }
    }
    return total;
}

static bool save_index_locked(void) {
    if (!s_storage_mounted) return true;

    char tmp_path[80];
    char idx_path[80];
    int n1 = snprintf(tmp_path, sizeof(tmp_path), "%s/index.tmp", s_storage_dir);
    int n2 = snprintf(idx_path, sizeof(idx_path), "%s/index.bin", s_storage_dir);
    if (n1 <= 0 || (size_t)n1 >= sizeof(tmp_path) || n2 <= 0 || (size_t)n2 >= sizeof(idx_path)) {
        return false;
    }

    FILE *fp = fopen(tmp_path, "wb");
    if (!fp) return false;

    ecard_index_header_t hdr = {
        .magic = INDEX_MAGIC,
        .version = INDEX_VERSION,
        .count = (uint16_t)s_count,
        .next_id = s_next_id,
    };
    bool ok = (fwrite(&hdr, sizeof(hdr), 1, fp) == 1);
    if (ok && s_count > 0) {
        ok = (fwrite(s_messages, sizeof(ecard_message_t), (size_t)s_count, fp) == (size_t)s_count);
    }
    if (fflush(fp) != 0) ok = false;
    if (fclose(fp) != 0) ok = false;

    if (!ok) {
        (void)remove(tmp_path);
        return false;
    }

    // SPIFFS 在目标文件已存在时 rename 可能失败，先删除旧索引再重命名。
    (void)remove(idx_path);
    if (rename(tmp_path, idx_path) != 0) {
        (void)remove(tmp_path);
        return false;
    }
    return true;
}

static bool load_index_locked(void) {
    if (!s_storage_mounted) return false;

    char idx_path[80];
    int n = snprintf(idx_path, sizeof(idx_path), "%s/index.bin", s_storage_dir);
    if (n <= 0 || (size_t)n >= sizeof(idx_path)) return false;

    FILE *fp = fopen(idx_path, "rb");
    if (!fp) return false;

    ecard_index_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1 ||
        hdr.magic != INDEX_MAGIC ||
        hdr.version != INDEX_VERSION ||
        hdr.count > ECARD_MESSAGES_MAX) {
        fclose(fp);
        return false;
    }

    memset(s_messages, 0, sizeof(s_messages));
    s_count = 0;
    if (hdr.count > 0) {
        if (fread(s_messages, sizeof(ecard_message_t), hdr.count, fp) != hdr.count) {
            fclose(fp);
            memset(s_messages, 0, sizeof(s_messages));
            s_count = 0;
            return false;
        }
        s_count = (int)hdr.count;
    }
    fclose(fp);

    s_next_id = hdr.next_id ? hdr.next_id : 1;
    uint32_t max_id = 0;
    for (int i = 0; i < s_count; ++i) {
        if (s_messages[i].id > max_id) max_id = s_messages[i].id;
        // 确保字符串以 NUL 结尾，并校验音频文件实际存在
        s_messages[i].peer[ECARD_MESSAGES_PEER_MAX - 1] = '\0';
        s_messages[i].sender_phone[ECARD_MESSAGES_PHONE_MAX - 1] = '\0';
        s_messages[i].text[ECARD_MESSAGES_TEXT_MAX - 1] = '\0';
        s_messages[i].remote_id[ECARD_MESSAGES_REMOTE_ID_MAX - 1] = '\0';
        if (s_messages[i].has_audio) {
            char apath[80];
            FILE *afp = build_audio_path_locked(s_messages[i].id, apath, sizeof(apath))
                            ? fopen(apath, "rb")
                            : NULL;
            if (!afp) {
                s_messages[i].has_audio = false;
                s_messages[i].audio_bytes = 0;
            } else {
                fclose(afp);
            }
        }
        // 在本轮开机尚未完成 SNTP/服务端对时前，先按索引顺序给出相对创建时间，
        // 一旦拿到真实时间戳会立即用 created_epoch_sec 精确重算并裁剪 24h 外条目。
        if (!s_epoch_synced) {
            s_messages[i].created_ms = (s_now_ms > (uint64_t)i) ? (s_now_ms - (uint64_t)i) : 0;
        } else if (s_messages[i].created_epoch_sec > 0) {
            const uint32_t age = age_sec_of_locked(&s_messages[i]);
            const uint64_t age_ms = (uint64_t)age * 1000ull;
            s_messages[i].created_ms = (age_ms >= s_now_ms) ? 0ull : s_now_ms - age_ms;
        }
    }
    if (s_next_id <= max_id) {
        s_next_id = max_id + 1;
        if (s_next_id == 0) s_next_id = 1;
    }
    return true;
}

static bool prune_expired_locked(void) {
    bool changed = false;
    int write_idx = 0;
    for (int i = 0; i < s_count; ++i) {
        if (age_sec_of_locked(&s_messages[i]) > ECARD_MESSAGES_WINDOW_SEC) {
            if (s_storage_mounted) {
                delete_audio_file_locked(s_messages[i].id);
            }
            changed = true;
            continue;
        }
        if (write_idx != i) {
            s_messages[write_idx] = s_messages[i];
        }
        ++write_idx;
    }
    if (write_idx < s_count) {
        memset(&s_messages[write_idx], 0,
               (size_t)(s_count - write_idx) * sizeof(ecard_message_t));
        s_count = write_idx;
    }
    return changed;
}

static void evict_for_budget_locked(uint32_t incoming_audio_bytes) {
    if (s_storage_mounted) {
        (void)prune_expired_locked();
    }
    while (s_count >= ECARD_MESSAGES_MAX && s_count > 0) {
        const uint32_t drop_id = s_messages[s_count - 1].id;
        delete_audio_file_locked(drop_id);
        --s_count;
        memset(&s_messages[s_count], 0, sizeof(ecard_message_t));
    }
    while (incoming_audio_bytes > 0 && s_count > 0 &&
           total_audio_bytes_locked() + incoming_audio_bytes > ECARD_MESSAGES_MAX_AUDIO_BYTES) {
        const uint32_t drop_id = s_messages[s_count - 1].id;
        delete_audio_file_locked(drop_id);
        --s_count;
        memset(&s_messages[s_count], 0, sizeof(ecard_message_t));
    }
}

static void insert_sorted_locked(const ecard_message_t *msg) {
    evict_for_budget_locked(msg->has_audio ? msg->audio_bytes : 0u);

    int pos = s_count;
    if (s_count < ECARD_MESSAGES_MAX) {
        ++s_count;
    } else {
        // 极端兜底
        s_count = ECARD_MESSAGES_MAX - 1;
        pos = s_count;
        ++s_count;
    }

    // 同一毫秒内追加多条时，后插入的视为更新，排在前面。
    while (pos > 0 && s_messages[pos - 1].created_ms <= msg->created_ms) {
        s_messages[pos] = s_messages[pos - 1];
        --pos;
    }
    s_messages[pos] = *msg;
}

static uint32_t alloc_id_locked(uint32_t requested_id) {
    if (requested_id != 0) return requested_id;
    uint32_t candidate = s_next_id++;
    if (s_next_id == 0) s_next_id = 1;
    // 防止碰撞
    for (;;) {
        bool clash = false;
        for (int i = 0; i < s_count; ++i) {
            if (s_messages[i].id == candidate) {
                clash = true;
                break;
            }
        }
        if (!clash && candidate != 0) return candidate;
        candidate = s_next_id++;
        if (s_next_id == 0) s_next_id = 1;
    }
}

static bool has_duplicate_locked(const ecard_message_t *msg, uint32_t *existing_id) {
    for (int i = 0; i < s_count; ++i) {
        if (msg->id != 0 && s_messages[i].id == msg->id) {
            if (existing_id) *existing_id = s_messages[i].id;
            return true;
        }
        if (msg->remote_id[0] != '\0' &&
            strcmp(s_messages[i].remote_id, msg->remote_id) == 0) {
            if (existing_id) *existing_id = s_messages[i].id;
            return true;
        }
    }
    return false;
}

static ecard_message_t s_pending_store; // 仅在 msg_lock() 内使用，避免大结构体压爆调用者任务栈

static uint32_t add_internal_locked(ecard_message_t *msg, uint32_t age_sec,
                                    const uint8_t *adpcm_data, size_t adpcm_len,
                                    const char *temp_audio_path) {
    if (!msg) return 0;

    uint32_t existing = 0;
    if (has_duplicate_locked(msg, &existing)) {
        if (temp_audio_path && temp_audio_path[0]) {
            (void)remove(temp_audio_path);
        }
        return existing;
    }

    uint32_t effective_age = age_sec;
    int64_t epoch_sec = msg->created_epoch_sec;
    if (s_epoch_synced) {
        if (epoch_sec > 0) {
            effective_age = (epoch_sec >= s_now_epoch) ? 0u
                                                       : (uint32_t)(s_now_epoch - epoch_sec);
        } else {
            epoch_sec = (s_now_epoch > (int64_t)age_sec)
                            ? (s_now_epoch - (int64_t)age_sec)
                            : 0;
        }
        if (s_storage_mounted && effective_age > ECARD_MESSAGES_WINDOW_SEC) {
            if (temp_audio_path && temp_audio_path[0]) {
                (void)remove(temp_audio_path);
            }
            return 0;
        }
    }

    const uint32_t assigned_id = alloc_id_locked(msg->id);
    msg->id = assigned_id;

    s_pending_store = *msg;
    s_pending_store.peer[ECARD_MESSAGES_PEER_MAX - 1] = '\0';
    s_pending_store.sender_phone[ECARD_MESSAGES_PHONE_MAX - 1] = '\0';
    s_pending_store.text[ECARD_MESSAGES_TEXT_MAX - 1] = '\0';
    s_pending_store.remote_id[ECARD_MESSAGES_REMOTE_ID_MAX - 1] = '\0';
    s_pending_store.created_epoch_sec = epoch_sec;

    const uint64_t age_ms = (uint64_t)effective_age * 1000ull;
    s_pending_store.created_ms = (age_ms >= s_now_ms) ? 0ull : s_now_ms - age_ms;
    if (s_pending_store.direction == ECARD_MESSAGE_SENT) {
        s_pending_store.unread = false;
        if (s_pending_store.sender_role == ECARD_SENDER_UNKNOWN) {
            s_pending_store.sender_role = ECARD_SENDER_STUDENT;
        }
    } else if (s_pending_store.sender_role == ECARD_SENDER_UNKNOWN) {
        s_pending_store.sender_role = ECARD_SENDER_PRIMARY_PARENT;
    }

    if (s_storage_mounted && (adpcm_data && adpcm_len > 0)) {
        evict_for_budget_locked((uint32_t)adpcm_len);
        char apath[80];
        if (build_audio_path_locked(assigned_id, apath, sizeof(apath))) {
            FILE *fp = fopen(apath, "wb");
            if (fp) {
                const size_t wrote = fwrite(adpcm_data, 1, adpcm_len, fp);
                (void)fflush(fp);
                (void)fclose(fp);
                if (wrote == adpcm_len) {
                    s_pending_store.has_audio = true;
                    s_pending_store.audio_bytes = (uint32_t)adpcm_len;
                } else {
                    (void)remove(apath);
                    s_pending_store.has_audio = false;
                    s_pending_store.audio_bytes = 0;
                }
            }
        }
    } else if (s_storage_mounted && temp_audio_path && temp_audio_path[0] && adpcm_len > 0) {
        evict_for_budget_locked((uint32_t)adpcm_len);
        char apath[80];
        if (build_audio_path_locked(assigned_id, apath, sizeof(apath))) {
            (void)remove(apath);
            if (rename(temp_audio_path, apath) == 0) {
                s_pending_store.has_audio = true;
                s_pending_store.audio_bytes = (uint32_t)adpcm_len;
            } else {
                (void)remove(temp_audio_path);
                s_pending_store.has_audio = false;
                s_pending_store.audio_bytes = 0;
            }
        } else {
            (void)remove(temp_audio_path);
        }
    } else if (temp_audio_path && temp_audio_path[0]) {
        (void)remove(temp_audio_path);
    }

    insert_sorted_locked(&s_pending_store);
    (void)save_index_locked();
    return s_pending_store.id;
}

static void seed_demo_messages_locked(void) {
    // 演示数据：有未读、有已读、有发出的，也有一条 26 小时前的旧留言，
    // 用来验证列表确实按 24 小时窗口过滤。
    ecard_message_t demo[] = {
        { .direction = ECARD_MESSAGE_RECEIVED, .sender_role = ECARD_SENDER_PRIMARY_PARENT,
          .peer = "妈妈", .duration_sec = 7, .unread = true,
          .text = "放学记得带上水杯，妈妈五点半来接你。" },
        { .direction = ECARD_MESSAGE_SENT, .sender_role = ECARD_SENDER_STUDENT,
          .peer = "妈妈", .duration_sec = 6,
          .text = "知道啦妈妈，我现在去操场排队。" },
        { .direction = ECARD_MESSAGE_RECEIVED, .sender_role = ECARD_SENDER_TEACHER,
          .peer = "王老师", .duration_sec = 9, .unread = true,
          .text = "请同学们明天带好美术课用的彩笔。" },
        { .direction = ECARD_MESSAGE_RECEIVED, .sender_role = ECARD_SENDER_FAMILY,
          .peer = "爸爸", .duration_sec = 12,
          .text = "晚上回家记得先喝口热牛奶再写作业。" },
        { .direction = ECARD_MESSAGE_SENT, .sender_role = ECARD_SENDER_STUDENT,
          .peer = "爸爸", .duration_sec = 6,
          .text = "好的爸爸，今天数学题我都做完啦。" },
        { .direction = ECARD_MESSAGE_RECEIVED, .sender_role = ECARD_SENDER_PRIMARY_PARENT,
          .peer = "妈妈", .duration_sec = 7,
          .text = "昨天这条留言已超过 24 小时。" },
    };
    static const uint32_t demo_age_sec[] = { 180u, 1680u, 7200u, 18000u, 32400u,
                                             93600u };

    for (size_t i = 0; i < sizeof(demo) / sizeof(demo[0]); ++i) {
        (void)add_internal_locked(&demo[i], demo_age_sec[i], NULL, 0, NULL);
    }
}

void ecard_messages_init(void) {
    msg_lock();
    memset(s_messages, 0, sizeof(s_messages));
    s_count = 0;
    s_now_ms = CLOCK_START_MS;
    s_next_id = 1;
    s_now_epoch = 0;
    s_epoch_synced = false;

    if (s_storage_mounted) {
        if (!load_index_locked()) {
            (void)save_index_locked();
        }
    } else {
        seed_demo_messages_locked();
    }
    msg_unlock();
}

bool ecard_messages_mount_dir(const char *dir) {
    msg_lock();
    if (!dir || !dir[0]) {
        s_storage_mounted = false;
        s_storage_dir[0] = '\0';
        msg_unlock();
        return true;
    }

    const size_t len = strlen(dir);
    if (len >= sizeof(s_storage_dir)) {
        msg_unlock();
        return false;
    }
    memcpy(s_storage_dir, dir, len + 1);
    while (len > 1 && s_storage_dir[strlen(s_storage_dir) - 1] == '/') {
        s_storage_dir[strlen(s_storage_dir) - 1] = '\0';
    }
    s_storage_mounted = true;

    memset(s_messages, 0, sizeof(s_messages));
    s_count = 0;
    s_now_ms = CLOCK_START_MS;
    s_next_id = 1;

    if (!load_index_locked()) {
        // 首次挂载空分区：写入空索引文件并校验可写性
        if (!save_index_locked()) {
            s_storage_mounted = false;
            s_storage_dir[0] = '\0';
            msg_unlock();
            return false;
        }
    }
    msg_unlock();
    return true;
}

bool ecard_messages_mount_spiffs(void) {
#if defined(ESP_PLATFORM)
    if (!s_spiffs_registered) {
        esp_vfs_spiffs_conf_t conf = {
            .base_path = "/spiffs",
            .partition_label = "storage",
            .max_files = 3,
            .format_if_mount_failed = true,
        };
        const esp_err_t err = esp_vfs_spiffs_register(&conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS 挂载失败: 0x%x", (unsigned)err);
            return false;
        }
        s_spiffs_registered = true;
        size_t total = 0;
        size_t used = 0;
        if (esp_spiffs_info("storage", &total, &used) == ESP_OK) {
            ESP_LOGI(TAG, "SPIFFS 挂载成功: 已用 %u / %u KB",
                     (unsigned)(used / 1024), (unsigned)(total / 1024));
        }
    }
    return ecard_messages_mount_dir("/spiffs");
#else
    return false;
#endif
}

bool ecard_messages_clear_all(void) {
    msg_lock();
    for (int i = 0; i < s_count; ++i) {
        if (s_storage_mounted) {
            delete_audio_file_locked(s_messages[i].id);
        }
    }
    if (s_storage_mounted) {
        char tmp_path[80];
        if (snprintf(tmp_path, sizeof(tmp_path), "%s/tmp_dl.adp", s_storage_dir) > 0) {
            (void)remove(tmp_path);
        }
        if (snprintf(tmp_path, sizeof(tmp_path), "%s/tmp_rec.adp", s_storage_dir) > 0) {
            (void)remove(tmp_path);
        }
    }
    memset(s_messages, 0, sizeof(s_messages));
    s_count = 0;
    s_next_id = 1;
    const bool ok = save_index_locked();
    msg_unlock();
    return ok;
}

void ecard_messages_tick(uint32_t delta_ms) {
    msg_lock();
    s_now_ms += delta_ms;
    if (s_epoch_synced && delta_ms > 0) {
        static uint32_t s_epoch_ms_rem = 0;
        s_epoch_ms_rem += delta_ms;
        if (s_epoch_ms_rem >= 1000u) {
            s_now_epoch += (int64_t)(s_epoch_ms_rem / 1000u);
            s_epoch_ms_rem %= 1000u;
        }
    }
    if (s_storage_mounted && prune_expired_locked()) {
        (void)save_index_locked();
    }
    msg_unlock();
}

void ecard_messages_sync_epoch(int64_t now_epoch_sec) {
    if (now_epoch_sec < VALID_EPOCH_MIN) return;

    msg_lock();
    s_now_epoch = now_epoch_sec;
    s_epoch_synced = true;

    bool changed = false;
    for (int i = 0; i < s_count; ++i) {
        if (s_messages[i].created_epoch_sec <= 0) {
            const uint32_t mono_age = (s_messages[i].created_ms >= s_now_ms)
                                          ? 0u
                                          : (uint32_t)((s_now_ms - s_messages[i].created_ms) / 1000ull);
            s_messages[i].created_epoch_sec =
                (now_epoch_sec > (int64_t)mono_age) ? (now_epoch_sec - (int64_t)mono_age) : now_epoch_sec;
            changed = true;
        } else {
            const uint32_t real_age = age_sec_of_locked(&s_messages[i]);
            const uint64_t age_ms = (uint64_t)real_age * 1000ull;
            s_messages[i].created_ms = (age_ms >= s_now_ms) ? 0ull : s_now_ms - age_ms;
        }
    }
    if (s_storage_mounted) {
        if (prune_expired_locked()) changed = true;
        if (changed) (void)save_index_locked();
    }
    msg_unlock();
}

bool ecard_messages_epoch_synced(void) {
    msg_lock();
    const bool v = s_epoch_synced;
    msg_unlock();
    return v;
}

uint64_t ecard_messages_now_ms(void) {
    msg_lock();
    const uint64_t v = s_now_ms;
    msg_unlock();
    return v;
}

uint32_t ecard_messages_add(ecard_message_t *msg, uint32_t age_sec) {
    msg_lock();
    const uint32_t id = add_internal_locked(msg, age_sec, NULL, 0, NULL);
    msg_unlock();
    return id;
}

uint32_t ecard_messages_add_with_audio(ecard_message_t *msg, uint32_t age_sec,
                                       const uint8_t *adpcm_data, size_t adpcm_len) {
    msg_lock();
    const uint32_t id = add_internal_locked(msg, age_sec, adpcm_data, adpcm_len, NULL);
    msg_unlock();
    return id;
}

uint32_t ecard_messages_adopt_audio_file(ecard_message_t *msg, uint32_t age_sec,
                                         const char *temp_audio_path, size_t adpcm_len) {
    msg_lock();
    const uint32_t id = add_internal_locked(msg, age_sec, NULL, adpcm_len, temp_audio_path);
    msg_unlock();
    return id;
}

bool ecard_messages_has_remote_id(const char *remote_id) {
    if (!remote_id || !remote_id[0]) return false;
    msg_lock();
    bool found = false;
    for (int i = 0; i < s_count; ++i) {
        if (strcmp(s_messages[i].remote_id, remote_id) == 0) {
            found = true;
            break;
        }
    }
    msg_unlock();
    return found;
}

bool ecard_messages_mark_uploaded(uint32_t local_id, const char *remote_id, const char *asr_text) {
    if (local_id == 0) return false;
    msg_lock();
    bool found = false;
    for (int i = 0; i < s_count; ++i) {
        if (s_messages[i].id != local_id) continue;
        s_messages[i].pending_upload = false;
        if (remote_id && remote_id[0]) {
            snprintf(s_messages[i].remote_id, sizeof(s_messages[i].remote_id), "%s", remote_id);
        }
        if (asr_text && asr_text[0]) {
            snprintf(s_messages[i].text, sizeof(s_messages[i].text), "%s", asr_text);
        }
        (void)save_index_locked();
        found = true;
        break;
    }
    msg_unlock();
    return found;
}

bool ecard_messages_next_pending_upload(ecard_message_t *out) {
    if (!out) return false;
    msg_lock();
    bool found = false;
    for (int i = s_count - 1; i >= 0; --i) {
        if (s_messages[i].direction == ECARD_MESSAGE_SENT &&
            s_messages[i].pending_upload &&
            s_messages[i].has_audio &&
            age_sec_of_locked(&s_messages[i]) <= ECARD_MESSAGES_WINDOW_SEC) {
            *out = s_messages[i];
            found = true;
            break;
        }
    }
    msg_unlock();
    return found;
}

bool ecard_messages_audio_path(uint32_t id, char *buf, size_t size) {
    msg_lock();
    const bool ok = build_audio_path_locked(id, buf, size);
    msg_unlock();
    return ok;
}

bool ecard_messages_temp_audio_path(char *buf, size_t size) {
    if (!buf || size == 0) return false;
    msg_lock();
    if (!s_storage_mounted) {
        msg_unlock();
        return false;
    }
    const int n = snprintf(buf, size, "%s/tmp_dl.adp", s_storage_dir);
    msg_unlock();
    return n > 0 && (size_t)n < size;
}

bool ecard_messages_rec_temp_audio_path(char *buf, size_t size) {
    if (!buf || size == 0) return false;
    msg_lock();
    if (!s_storage_mounted) {
        msg_unlock();
        return false;
    }
    const int n = snprintf(buf, size, "%s/tmp_rec.adp", s_storage_dir);
    msg_unlock();
    return n > 0 && (size_t)n < size;
}

const ecard_message_t *ecard_messages_recent(int index) {
    if (index < 0) return NULL;

    msg_lock();
    const ecard_message_t *ret = NULL;
    int seen = 0;
    for (int i = 0; i < s_count; ++i) {
        if (age_sec_of_locked(&s_messages[i]) > ECARD_MESSAGES_WINDOW_SEC) continue;
        if (seen == index) {
            ret = &s_messages[i];
            break;
        }
        ++seen;
    }
    msg_unlock();
    return ret;
}

bool ecard_messages_copy_recent(int index, ecard_message_t *out) {
    if (index < 0 || !out) return false;
    msg_lock();
    bool ok = false;
    int seen = 0;
    for (int i = 0; i < s_count; ++i) {
        if (age_sec_of_locked(&s_messages[i]) > ECARD_MESSAGES_WINDOW_SEC) continue;
        if (seen == index) {
            *out = s_messages[i];
            ok = true;
            break;
        }
        ++seen;
    }
    msg_unlock();
    return ok;
}

bool ecard_messages_copy_by_id(uint32_t id, ecard_message_t *out) {
    if (id == 0 || !out) return false;
    msg_lock();
    bool ok = false;
    for (int i = 0; i < s_count; ++i) {
        if (s_messages[i].id == id) {
            *out = s_messages[i];
            ok = true;
            break;
        }
    }
    msg_unlock();
    return ok;
}

int ecard_messages_recent_count(void) {
    msg_lock();
    int seen = 0;
    for (int i = 0; i < s_count; ++i) {
        if (age_sec_of_locked(&s_messages[i]) > ECARD_MESSAGES_WINDOW_SEC) continue;
        ++seen;
    }
    msg_unlock();
    return seen;
}

const ecard_message_t *ecard_messages_latest_unread(void) {
    msg_lock();
    const ecard_message_t *ret = NULL;
    for (int i = 0; i < s_count; ++i) {
        if (age_sec_of_locked(&s_messages[i]) > ECARD_MESSAGES_WINDOW_SEC) continue;
        if (s_messages[i].direction == ECARD_MESSAGE_RECEIVED && s_messages[i].unread) {
            ret = &s_messages[i];
            break;
        }
    }
    msg_unlock();
    return ret;
}

bool ecard_messages_copy_latest_unread(ecard_message_t *out) {
    if (!out) return false;
    msg_lock();
    bool ok = false;
    for (int i = 0; i < s_count; ++i) {
        if (age_sec_of_locked(&s_messages[i]) > ECARD_MESSAGES_WINDOW_SEC) continue;
        if (s_messages[i].direction == ECARD_MESSAGE_RECEIVED && s_messages[i].unread) {
            *out = s_messages[i];
            ok = true;
            break;
        }
    }
    msg_unlock();
    return ok;
}

int ecard_messages_unread_count(void) {
    msg_lock();
    int seen = 0;
    for (int i = 0; i < s_count; ++i) {
        if (age_sec_of_locked(&s_messages[i]) > ECARD_MESSAGES_WINDOW_SEC) continue;
        if (s_messages[i].direction == ECARD_MESSAGE_RECEIVED && s_messages[i].unread) {
            ++seen;
        }
    }
    msg_unlock();
    return seen;
}

bool ecard_messages_mark_read(uint32_t id) {
    msg_lock();
    bool changed = false;
    for (int i = 0; i < s_count; ++i) {
        if (s_messages[i].id != id) continue;
        if (!s_messages[i].unread) break;
        s_messages[i].unread = false;
        (void)save_index_locked();
        changed = true;
        break;
    }
    msg_unlock();
    return changed;
}

void ecard_messages_age_label(uint64_t created_ms, char *buf, size_t size) {
    if (!buf || size == 0) return;
    buf[0] = '\0';

    msg_lock();
    const uint64_t now_ms = s_now_ms;
    msg_unlock();

    const uint32_t age = (created_ms >= now_ms) ? 0
                                                : (uint32_t)((now_ms - created_ms) / 1000ull);
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

const char *ecard_messages_role_label(ecard_sender_role_t role) {
    switch (role) {
    case ECARD_SENDER_PRIMARY_PARENT:
        return "主家长";
    case ECARD_SENDER_FAMILY:
        return "家庭成员";
    case ECARD_SENDER_TEACHER:
        return "班级老师";
    case ECARD_SENDER_SCHOOL_ADMIN:
        return "学校管理";
    case ECARD_SENDER_STUDENT:
        return "我发出";
    case ECARD_SENDER_UNKNOWN:
    default:
        return "家长";
    }
}

const char *ecard_messages_role_badge(ecard_sender_role_t role) {
    switch (role) {
    case ECARD_SENDER_TEACHER:
        return "师";
    case ECARD_SENDER_SCHOOL_ADMIN:
        return "校";
    case ECARD_SENDER_STUDENT:
        return "我";
    case ECARD_SENDER_PRIMARY_PARENT:
    case ECARD_SENDER_FAMILY:
    case ECARD_SENDER_UNKNOWN:
    default:
        return "家";
    }
}

void ecard_messages_get_storage_stats(ecard_messages_storage_stats_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    msg_lock();
    out->storage_mounted = s_storage_mounted;
    out->max_messages = ECARD_MESSAGES_MAX;
    out->max_audio_bytes = ECARD_MESSAGES_MAX_AUDIO_BYTES;
    int valid_cnt = 0;
    uint32_t audio_sum = 0;
    for (int i = 0; i < s_count; ++i) {
        if (age_sec_of_locked(&s_messages[i]) <= ECARD_MESSAGES_WINDOW_SEC) {
            ++valid_cnt;
        }
        if (s_messages[i].has_audio) {
            audio_sum += s_messages[i].audio_bytes;
        }
    }
    out->message_count = (uint16_t)valid_cnt;
    out->total_audio_bytes = audio_sum;
    msg_unlock();

#if defined(ESP_PLATFORM)
    if (s_spiffs_registered) {
        size_t total = 0;
        size_t used = 0;
        if (esp_spiffs_info("storage", &total, &used) == ESP_OK) {
            out->fs_total_bytes = (uint32_t)total;
            out->fs_used_bytes = (uint32_t)used;
        }
    }
#endif
}
