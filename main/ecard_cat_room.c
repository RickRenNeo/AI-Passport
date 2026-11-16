#include "ecard_cat_room.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <nvs.h>
#include <nvs_flash.h>
#define ROOM_NVS_NAMESPACE "ecard_room"
#define ROOM_NVS_KEY "state_v1"
#endif

#define PAIR_CODE_LEN 8
#define INVITE_CODE_LEN 8
#define TOKEN_HEX_LEN 32
#define PARENT_ID_MAX 48
#define PARENT_LABEL_MAX 24
#define PAIR_FAIL_LIMIT 5u
#define PAIR_LOCKOUT_MS 60000ULL
#define STATE_MAGIC 0x43415452u /* 'CATR' */
#define STATE_VERSION 1u

typedef struct {
    bool active;
    char parent_id[PARENT_ID_MAX];
    char label[PARENT_LABEL_MAX];
    char token[TOKEN_HEX_LEN + 1];
} parent_slot_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t pending_points;
    uint16_t claimed_points;
    uint16_t _reserved;
    uint32_t completed_sessions;
    uint32_t unlocked_mask;
    uint8_t equipped[ECARD_ROOM_SLOT_COUNT];
    char pair_code[PAIR_CODE_LEN + 1];
    char invite_code[INVITE_CODE_LEN + 1];
    parent_slot_t primary;
    parent_slot_t secondary[ECARD_ROOM_MAX_SECONDARY_PARENTS];
} cat_room_state_t;

static const char *const k_slot_names[ECARD_ROOM_SLOT_COUNT] = {
    "wall",
    "wall_decor",
    "chair",
    "lamp",
    "desk_item",
    "rug",
};

static const ecard_room_item_t k_catalog[ECARD_ROOM_ITEM_COUNT] = {
    // wall (0..3)
    { "wall_cream",  ECARD_ROOM_SLOT_WALL, 0, "暖杏素墙", 0 },
    { "wall_sky",    ECARD_ROOM_SLOT_WALL, 1, "星空蓝墙", 15 },
    { "wall_sakura", ECARD_ROOM_SLOT_WALL, 2, "樱花粉墙", 20 },
    { "wall_sage",   ECARD_ROOM_SLOT_WALL, 3, "森林黑板", 30 },
    // wall_decor (4..7)
    { "wdecor_window",  ECARD_ROOM_SLOT_WALL_DECOR, 0, "晴空小窗", 0 },
    { "wdecor_board",   ECARD_ROOM_SLOT_WALL_DECOR, 1, "软木照片墙", 15 },
    { "wdecor_bunting", ECARD_ROOM_SLOT_WALL_DECOR, 2, "彩虹三角旗", 25 },
    { "wdecor_clock",   ECARD_ROOM_SLOT_WALL_DECOR, 3, "小猫挂钟", 35 },
    // chair (8..11)
    { "chair_wood",     ECARD_ROOM_SLOT_CHAIR, 0, "原木小方椅", 0 },
    { "chair_cat_pink", ECARD_ROOM_SLOT_CHAIR, 1, "猫耳软垫椅", 20 },
    { "chair_cloud",    ECARD_ROOM_SLOT_CHAIR, 2, "云朵豆袋椅", 30 },
    { "chair_crown",    ECARD_ROOM_SLOT_CHAIR, 3, "丝绒皇冠椅", 40 },
    // lamp (12..15)
    { "lamp_none",     ECARD_ROOM_SLOT_LAMP, 0, "无台灯", 0 },
    { "lamp_mushroom", ECARD_ROOM_SLOT_LAMP, 1, "暖黄蘑菇灯", 15 },
    { "lamp_moon",     ECARD_ROOM_SLOT_LAMP, 2, "弯月星星灯", 25 },
    { "lamp_bear",     ECARD_ROOM_SLOT_LAMP, 3, "小熊护眼灯", 35 },
    // desk_item (16..19)
    { "desk_pencil",    ECARD_ROOM_SLOT_DESK_ITEM, 0, "木质笔筒", 0 },
    { "desk_cake_mug",  ECARD_ROOM_SLOT_DESK_ITEM, 1, "甜心蛋糕杯", 10 },
    { "desk_succulent", ECARD_ROOM_SLOT_DESK_ITEM, 2, "多肉小盆栽", 20 },
    { "desk_lucky_cat", ECARD_ROOM_SLOT_DESK_ITEM, 3, "招财猫摆件", 30 },
    // rug (20..23)
    { "rug_none",    ECARD_ROOM_SLOT_RUG, 0, "无地毯", 0 },
    { "rug_dot",     ECARD_ROOM_SLOT_RUG, 1, "奶油波点毯", 15 },
    { "rug_rainbow", ECARD_ROOM_SLOT_RUG, 2, "彩虹条纹毯", 25 },
    { "rug_paw",     ECARD_ROOM_SLOT_RUG, 3, "猫爪毛绒毯", 35 },
};

static cat_room_state_t s_state;
static bool s_loaded;
static bool s_pairing_active;
static uint8_t s_bind_fails;
static uint64_t s_bind_lockout_until_ms;
static uint8_t s_join_fails;
static uint64_t s_join_lockout_until_ms;
#if defined(ESP_PLATFORM)
static SemaphoreHandle_t s_mutex;
#else
static uint32_t s_host_prng = 0x5A17C301u;
static uint64_t s_host_now_ms;
#endif

static void room_lock(void) {
#if defined(ESP_PLATFORM)
    if (s_mutex) (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
#endif
}

static void room_unlock(void) {
#if defined(ESP_PLATFORM)
    if (s_mutex) (void)xSemaphoreGive(s_mutex);
#endif
}

static uint64_t now_ms(void) {
#if defined(ESP_PLATFORM)
    return (uint64_t)(esp_timer_get_time() / 1000LL);
#else
    return s_host_now_ms;
#endif
}

#if !defined(ESP_PLATFORM)
void ecard_cat_room_set_now_ms(uint64_t ms) {
    s_host_now_ms = ms;
}
#endif

static uint32_t next_rand32(void) {
#if defined(ESP_PLATFORM)
    return esp_random();
#else
    uint32_t x = s_host_prng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_host_prng = x;
    return x;
#endif
}

static void gen_alpha_code(char *out, size_t len) {
    static const char k_alpha[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    const size_t alpha_n = sizeof(k_alpha) - 1;
    for (size_t i = 0; i < len; ++i) {
        out[i] = k_alpha[next_rand32() % alpha_n];
    }
    out[len] = '\0';
}

static void gen_hex_token(char *out) {
    static const char k_hex[] = "0123456789abcdef";
    for (size_t i = 0; i < TOKEN_HEX_LEN; ++i) {
        out[i] = k_hex[next_rand32() & 0x0Fu];
    }
    out[TOKEN_HEX_LEN] = '\0';
}

static uint32_t item_bit(ecard_room_slot_t slot, uint8_t variant) {
    const uint32_t idx = (uint32_t)slot * ECARD_ROOM_VARIANTS_PER_SLOT + variant;
    return 1u << idx;
}

static void init_defaults(void) {
    memset(&s_state, 0, sizeof(s_state));
    s_state.magic = STATE_MAGIC;
    s_state.version = STATE_VERSION;
    for (int s = 0; s < ECARD_ROOM_SLOT_COUNT; ++s) {
        s_state.unlocked_mask |= item_bit((ecard_room_slot_t)s, 0);
        s_state.equipped[s] = 0;
    }
    gen_alpha_code(s_state.pair_code, PAIR_CODE_LEN);
    s_bind_fails = 0;
    s_bind_lockout_until_ms = 0;
    s_join_fails = 0;
    s_join_lockout_until_ms = 0;
}

static void save_state(void) {
#if defined(ESP_PLATFORM)
    nvs_handle_t handle;
    if (nvs_open(ROOM_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    if (nvs_set_blob(handle, ROOM_NVS_KEY, &s_state, sizeof(s_state)) == ESP_OK) {
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
#endif
}

static void load_locked(void) {
    init_defaults();
#if defined(ESP_PLATFORM)
    nvs_handle_t handle;
    if (nvs_open(ROOM_NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        cat_room_state_t loaded;
        size_t size = sizeof(loaded);
        if (nvs_get_blob(handle, ROOM_NVS_KEY, &loaded, &size) == ESP_OK &&
            size == sizeof(loaded) &&
            loaded.magic == STATE_MAGIC &&
            loaded.version == STATE_VERSION) {
            s_state = loaded;
            for (int s = 0; s < ECARD_ROOM_SLOT_COUNT; ++s) {
                s_state.unlocked_mask |= item_bit((ecard_room_slot_t)s, 0);
                if (s_state.equipped[s] >= ECARD_ROOM_VARIANTS_PER_SLOT) {
                    s_state.equipped[s] = 0;
                }
            }
        }
        nvs_close(handle);
    }
#endif
    if (s_state.pair_code[0] == '\0') {
        gen_alpha_code(s_state.pair_code, PAIR_CODE_LEN);
    }
    s_loaded = true;
}

static void ensure_loaded_locked(void) {
    if (!s_loaded) {
        load_locked();
    }
}

static void copy_str(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src) return;
    size_t len = strlen(src);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static uint8_t parent_count_locked(void) {
    if (!s_state.primary.active) return 0;
    uint8_t count = 1;
    for (int i = 0; i < ECARD_ROOM_MAX_SECONDARY_PARENTS; ++i) {
        if (s_state.secondary[i].active) ++count;
    }
    return count;
}

static bool is_unlocked_locked(ecard_room_slot_t slot, uint8_t variant) {
    if (slot < 0 || slot >= ECARD_ROOM_SLOT_COUNT ||
        variant >= ECARD_ROOM_VARIANTS_PER_SLOT) {
        return false;
    }
    return (s_state.unlocked_mask & item_bit(slot, variant)) != 0;
}

void ecard_cat_room_init(void) {
#if defined(ESP_PLATFORM)
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
#endif
    room_lock();
    load_locked();
    room_unlock();
}

void ecard_cat_room_reset_all(void) {
#if defined(ESP_PLATFORM)
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
#endif
    room_lock();
    init_defaults();
    s_pairing_active = false;
    s_loaded = true;
#if defined(ESP_PLATFORM)
    nvs_handle_t handle;
    if (nvs_open(ROOM_NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        (void)nvs_erase_key(handle, ROOM_NVS_KEY);
        (void)nvs_commit(handle);
        nvs_close(handle);
    }
#endif
    room_unlock();
}

uint16_t ecard_cat_room_award_focus_minutes(uint8_t focus_minutes) {
    if (focus_minutes == 0) return 0;
    room_lock();
    ensure_loaded_locked();
    const uint16_t pts = (uint16_t)(((uint32_t)focus_minutes * 2u) / 5u);
    if (pts > 0) {
        s_state.pending_points = (uint16_t)(s_state.pending_points + pts);
    }
    ++s_state.completed_sessions;
    save_state();
    room_unlock();
    return pts;
}

uint16_t ecard_cat_room_pending_points(void) {
    room_lock();
    ensure_loaded_locked();
    const uint16_t v = s_state.pending_points;
    room_unlock();
    return v;
}

uint16_t ecard_cat_room_claimed_points(void) {
    room_lock();
    ensure_loaded_locked();
    const uint16_t v = s_state.claimed_points;
    room_unlock();
    return v;
}

uint32_t ecard_cat_room_completed_sessions(void) {
    room_lock();
    ensure_loaded_locked();
    const uint32_t v = s_state.completed_sessions;
    room_unlock();
    return v;
}

uint8_t ecard_cat_room_equipped(ecard_room_slot_t slot) {
    if (slot < 0 || slot >= ECARD_ROOM_SLOT_COUNT) return 0;
    room_lock();
    ensure_loaded_locked();
    const uint8_t v = s_state.equipped[slot];
    room_unlock();
    return v;
}

bool ecard_cat_room_is_unlocked(ecard_room_slot_t slot, uint8_t variant) {
    room_lock();
    ensure_loaded_locked();
    const bool v = is_unlocked_locked(slot, variant);
    room_unlock();
    return v;
}

const ecard_room_item_t *ecard_cat_room_item(ecard_room_slot_t slot, uint8_t variant) {
    if (slot < 0 || slot >= ECARD_ROOM_SLOT_COUNT ||
        variant >= ECARD_ROOM_VARIANTS_PER_SLOT) {
        return NULL;
    }
    return &k_catalog[(int)slot * ECARD_ROOM_VARIANTS_PER_SLOT + variant];
}

const char *ecard_cat_room_slot_name(ecard_room_slot_t slot) {
    if (slot < 0 || slot >= ECARD_ROOM_SLOT_COUNT) return "";
    return k_slot_names[slot];
}

bool ecard_cat_room_is_bound(void) {
    room_lock();
    ensure_loaded_locked();
    const bool v = s_state.primary.active;
    room_unlock();
    return v;
}

uint8_t ecard_cat_room_parent_count(void) {
    room_lock();
    ensure_loaded_locked();
    const uint8_t v = parent_count_locked();
    room_unlock();
    return v;
}

void ecard_cat_room_get_pair_code(char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    room_lock();
    ensure_loaded_locked();
    copy_str(out, out_size, s_state.pair_code);
    room_unlock();
}

void ecard_cat_room_rotate_pair_code(void) {
    room_lock();
    ensure_loaded_locked();
    char prev[PAIR_CODE_LEN + 1];
    copy_str(prev, sizeof(prev), s_state.pair_code);
    for (int tries = 0; tries < 4; ++tries) {
        gen_alpha_code(s_state.pair_code, PAIR_CODE_LEN);
        if (strcmp(prev, s_state.pair_code) != 0) break;
    }
    save_state();
    room_unlock();
}

void ecard_cat_room_set_pairing_active(bool active) {
    room_lock();
    s_pairing_active = active;
    room_unlock();
}

bool ecard_cat_room_is_pairing_active(void) {
    room_lock();
    const bool v = s_pairing_active;
    room_unlock();
    return v;
}

/* ------------------------------------------------------------------------- */
/* 极简零依赖 JSON 解析与格式化                                              */
/* ------------------------------------------------------------------------- */

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool overflow;
} str_writer_t;

static void sw_init(str_writer_t *w, char *buf, size_t cap) {
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->overflow = (cap == 0);
    if (cap > 0) buf[0] = '\0';
}

static void sw_raw(str_writer_t *w, const char *s) {
    if (w->overflow || !s) return;
    const size_t n = strlen(s);
    if (w->len + n + 1 > w->cap) {
        w->overflow = true;
        return;
    }
    memcpy(w->buf + w->len, s, n);
    w->len += n;
    w->buf[w->len] = '\0';
}

static void sw_json_str(str_writer_t *w, const char *s) {
    sw_raw(w, "\"");
    if (s) {
        for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
            if (*p == '"') {
                sw_raw(w, "\\\"");
            } else if (*p == '\\') {
                sw_raw(w, "\\\\");
            } else if (*p == '\n') {
                sw_raw(w, "\\n");
            } else if (*p == '\r') {
                sw_raw(w, "\\r");
            } else if (*p == '\t') {
                sw_raw(w, "\\t");
            } else if (*p < 0x20) {
                char esc[8];
                snprintf(esc, sizeof(esc), "\\u%04x", (unsigned)*p);
                sw_raw(w, esc);
            } else {
                char ch[2] = { (char)*p, '\0' };
                sw_raw(w, ch);
            }
        }
    }
    sw_raw(w, "\"");
}

static void sw_uint(str_writer_t *w, unsigned long v) {
    char num[24];
    snprintf(num, sizeof(num), "%lu", v);
    sw_raw(w, num);
}

static const char *skip_ws(const char *p) {
    while (p && *p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        ++p;
    }
    return p;
}

static bool is_json_delim(char c) {
    return c == '\0' || c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == ',' || c == '}';
}

static const char *find_json_value(const char *json, const char *key) {
    if (!json || !key) return NULL;
    const size_t key_len = strlen(key);
    const char *p = json;
    int obj_depth = 0;
    int arr_depth = 0;
    while (*p) {
        if (*p == '{') {
            ++obj_depth;
            ++p;
        } else if (*p == '}') {
            if (obj_depth > 0) --obj_depth;
            ++p;
        } else if (*p == '[') {
            ++arr_depth;
            ++p;
        } else if (*p == ']') {
            if (arr_depth > 0) --arr_depth;
            ++p;
        } else if (*p == '"') {
            ++p;
            const char *start = p;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p += 2;
                else ++p;
            }
            const size_t tok_len = (size_t)(p - start);
            if (*p == '"') ++p;
            const char *after = skip_ws(p);
            if (obj_depth == 1 && arr_depth == 0 && *after == ':' &&
                tok_len == key_len && strncmp(start, key, key_len) == 0) {
                return skip_ws(after + 1);
            }
        } else {
            ++p;
        }
    }
    return NULL;
}

static bool json_get_str(const char *json, const char *key, char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    const char *v = find_json_value(json, key);
    if (!v || *v != '"') return false;
    ++v;
    size_t idx = 0;
    while (*v && *v != '"') {
        char c = *v++;
        if (c == '\\' && *v) {
            const char esc = *v++;
            switch (esc) {
            case '"': c = '"'; break;
            case '\\': c = '\\'; break;
            case '/': c = '/'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            default: c = esc; break;
            }
        }
        if (idx + 1 >= out_size) {
            out[0] = '\0';
            return false;
        }
        out[idx++] = c;
    }
    if (*v != '"') {
        out[0] = '\0';
        return false;
    }
    out[idx] = '\0';
    return true;
}

static bool json_get_bool(const char *json, const char *key, bool *out) {
    const char *v = find_json_value(json, key);
    if (!v) return false;
    if (strncmp(v, "true", 4) == 0 && is_json_delim(v[4])) {
        *out = true;
        return true;
    }
    if (strncmp(v, "false", 5) == 0 && is_json_delim(v[5])) {
        *out = false;
        return true;
    }
    return false;
}

bool ecard_cat_room_build_qr_payload(const char *device_id, const char *sta_ip,
                                     uint16_t port, char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    room_lock();
    ensure_loaded_locked();
    str_writer_t w;
    sw_init(&w, out, out_size);
    sw_raw(&w, "{\"v\":1,\"id\":");
    sw_json_str(&w, device_id ? device_id : "ecard");
    sw_raw(&w, ",\"code\":");
    sw_json_str(&w, s_state.pair_code);
    sw_raw(&w, ",\"ip\":");
    sw_json_str(&w, (sta_ip && sta_ip[0]) ? sta_ip : "0.0.0.0");
    sw_raw(&w, ",\"port\":");
    sw_uint(&w, port ? port : 80u);
    sw_raw(&w, "}");
    room_unlock();
    return !w.overflow;
}

/* ------------------------------------------------------------------------- */
/* HTTP 鉴权与路由实现                                                        */
/* ------------------------------------------------------------------------- */

static int make_error(char *resp, size_t resp_size, int status, const char *err) {
    str_writer_t w;
    sw_init(&w, resp, resp_size);
    sw_raw(&w, "{\"ok\":false,\"error\":");
    sw_json_str(&w, err);
    sw_raw(&w, "}");
    return status;
}

static int finish_json(const str_writer_t *w, char *resp_buf, size_t resp_size) {
    if (w->overflow) {
        return make_error(resp_buf, resp_size, 500, "resp_overflow");
    }
    return 200;
}

static bool constant_time_eq(const char *a, const char *b) {
    if (!a || !b) return false;
    const size_t la = strlen(a);
    const size_t lb = strlen(b);
    if (la == 0 || la != lb) return false;
    unsigned diff = 0;
    for (size_t i = 0; i < la; ++i) {
        diff |= (unsigned)((unsigned char)a[i] ^ (unsigned char)b[i]);
    }
    return diff == 0;
}

static void extract_token(const char *auth_header, const char *body,
                          char *out, size_t out_size) {
    out[0] = '\0';
    if (auth_header && auth_header[0]) {
        const char *p = skip_ws(auth_header);
        if ((p[0] == 'B' || p[0] == 'b') &&
            (p[1] == 'e' || p[1] == 'E') &&
            (p[2] == 'a' || p[2] == 'A') &&
            (p[3] == 'r' || p[3] == 'R') &&
            (p[4] == 'e' || p[4] == 'E') &&
            (p[5] == 'r' || p[5] == 'R') &&
            (p[6] == ' ' || p[6] == '\t')) {
            p = skip_ws(p + 7);
            size_t i = 0;
            while (p[i] && !isspace((unsigned char)p[i]) && i + 1 < out_size) {
                out[i] = p[i];
                ++i;
            }
            out[i] = '\0';
            if (out[0]) return;
        }
    }
    (void)json_get_str(body, "token", out, out_size);
}

static parent_slot_t *authenticate_locked(const char *auth_header, const char *body,
                                          bool *out_is_primary) {
    char token[64];
    extract_token(auth_header, body, token, sizeof(token));
    if (!token[0]) return NULL;

    if (s_state.primary.active && constant_time_eq(token, s_state.primary.token)) {
        if (out_is_primary) *out_is_primary = true;
        return &s_state.primary;
    }
    for (int i = 0; i < ECARD_ROOM_MAX_SECONDARY_PARENTS; ++i) {
        if (s_state.secondary[i].active &&
            constant_time_eq(token, s_state.secondary[i].token)) {
            if (out_is_primary) *out_is_primary = false;
            return &s_state.secondary[i];
        }
    }
    return NULL;
}

static const ecard_room_item_t *find_item_by_id(const char *item_id) {
    if (!item_id || !item_id[0]) return NULL;
    for (int i = 0; i < ECARD_ROOM_ITEM_COUNT; ++i) {
        if (strcmp(k_catalog[i].id, item_id) == 0) {
            return &k_catalog[i];
        }
    }
    return NULL;
}

static bool path_is(const char *path, const char *expected) {
    if (!path || !expected) return false;
    const size_t n = strlen(expected);
    if (strncmp(path, expected, n) != 0) return false;
    return path[n] == '\0' || path[n] == '?';
}

static int handle_http_locked(const char *method, const char *path,
                              const char *auth_header, const char *body,
                              const char *device_id, const char *sta_ip,
                              uint16_t port, char *resp_buf, size_t resp_size,
                              bool *out_ui_dirty) {
    ensure_loaded_locked();
    if (out_ui_dirty) *out_ui_dirty = false;
    if (!method || !path || !resp_buf || resp_size == 0) {
        return 400;
    }
    const char *dev_id = (device_id && device_id[0]) ? device_id : "ecard";
    const char *ip_str = (sta_ip && sta_ip[0]) ? sta_ip : "0.0.0.0";
    const uint16_t port_num = port ? port : 80u;
    const bool is_get = (strcmp(method, "GET") == 0);
    const bool is_post = (strcmp(method, "POST") == 0);
    const uint64_t now = now_ms();

    // 1. GET /api/pair/status (公开摘要，绝不暴露配对码/邀请码/parent_id/token)
    if (is_get && path_is(path, "/api/pair/status")) {
        str_writer_t w;
        sw_init(&w, resp_buf, resp_size);
        sw_raw(&w, "{\"ok\":true,\"bound\":");
        sw_raw(&w, s_state.primary.active ? "true" : "false");
        sw_raw(&w, ",\"parent_count\":");
        sw_uint(&w, parent_count_locked());
        sw_raw(&w, ",\"device_id\":");
        sw_json_str(&w, dev_id);
        sw_raw(&w, ",\"ip\":");
        sw_json_str(&w, ip_str);
        sw_raw(&w, ",\"port\":");
        sw_uint(&w, port_num);
        sw_raw(&w, "}");
        return finish_json(&w, resp_buf, resp_size);
    }

    // 2. POST /api/pair/bind (仅在配对页开启期间允许绑定主家长，带防爆破限流)
    if (is_post && path_is(path, "/api/pair/bind")) {
        if (s_state.primary.active) {
            return make_error(resp_buf, resp_size, 409, "already_bound");
        }
        if (!s_pairing_active) {
            return make_error(resp_buf, resp_size, 403, "pairing_window_closed");
        }
        if (s_bind_lockout_until_ms != 0 && now < s_bind_lockout_until_ms) {
            return make_error(resp_buf, resp_size, 429, "rate_limited");
        }
        char code[24];
        char parent_id[PARENT_ID_MAX];
        char label[PARENT_LABEL_MAX];
        if (!json_get_str(body, "code", code, sizeof(code)) ||
            !json_get_str(body, "parent_id", parent_id, sizeof(parent_id)) ||
            !parent_id[0]) {
            return make_error(resp_buf, resp_size, 400, "invalid_params");
        }
        if (!constant_time_eq(code, s_state.pair_code)) {
            if (++s_bind_fails >= PAIR_FAIL_LIMIT) {
                s_bind_fails = 0;
                s_bind_lockout_until_ms = now + PAIR_LOCKOUT_MS;
                gen_alpha_code(s_state.pair_code, PAIR_CODE_LEN);
                save_state();
                if (out_ui_dirty) *out_ui_dirty = true;
            }
            return make_error(resp_buf, resp_size, 403, "invalid_pair_code");
        }
        if (!json_get_str(body, "label", label, sizeof(label))) {
            copy_str(label, sizeof(label), "主家长");
        }

        s_bind_fails = 0;
        s_bind_lockout_until_ms = 0;
        s_state.primary.active = true;
        copy_str(s_state.primary.parent_id, sizeof(s_state.primary.parent_id), parent_id);
        copy_str(s_state.primary.label, sizeof(s_state.primary.label), label);
        gen_hex_token(s_state.primary.token);
        gen_alpha_code(s_state.invite_code, INVITE_CODE_LEN);
        gen_alpha_code(s_state.pair_code, PAIR_CODE_LEN);
        save_state();
        if (out_ui_dirty) *out_ui_dirty = true;

        str_writer_t w;
        sw_init(&w, resp_buf, resp_size);
        sw_raw(&w, "{\"ok\":true,\"role\":\"primary\",\"token\":");
        sw_json_str(&w, s_state.primary.token);
        sw_raw(&w, ",\"invite_code\":");
        sw_json_str(&w, s_state.invite_code);
        sw_raw(&w, ",\"parent_count\":");
        sw_uint(&w, parent_count_locked());
        sw_raw(&w, "}");
        return finish_json(&w, resp_buf, resp_size);
    }

    // 3. POST /api/pair/join (带邀请码错误限流)
    if (is_post && path_is(path, "/api/pair/join")) {
        if (!s_state.primary.active) {
            return make_error(resp_buf, resp_size, 409, "not_bound");
        }
        if (s_join_lockout_until_ms != 0 && now < s_join_lockout_until_ms) {
            return make_error(resp_buf, resp_size, 429, "rate_limited");
        }
        char invite[24];
        char parent_id[PARENT_ID_MAX];
        char label[PARENT_LABEL_MAX];
        if (!json_get_str(body, "invite_code", invite, sizeof(invite)) ||
            !json_get_str(body, "parent_id", parent_id, sizeof(parent_id)) ||
            !parent_id[0]) {
            return make_error(resp_buf, resp_size, 400, "invalid_params");
        }
        if (!constant_time_eq(invite, s_state.invite_code)) {
            if (++s_join_fails >= PAIR_FAIL_LIMIT) {
                s_join_fails = 0;
                s_join_lockout_until_ms = now + PAIR_LOCKOUT_MS;
            }
            return make_error(resp_buf, resp_size, 403, "invalid_invite_code");
        }
        if (strcmp(s_state.primary.parent_id, parent_id) == 0) {
            return make_error(resp_buf, resp_size, 409, "already_primary");
        }
        if (!json_get_str(body, "label", label, sizeof(label))) {
            copy_str(label, sizeof(label), "家长");
        }

        int slot_idx = -1;
        for (int i = 0; i < ECARD_ROOM_MAX_SECONDARY_PARENTS; ++i) {
            if (s_state.secondary[i].active &&
                strcmp(s_state.secondary[i].parent_id, parent_id) == 0) {
                slot_idx = i;
                break;
            }
            if (!s_state.secondary[i].active && slot_idx < 0) {
                slot_idx = i;
            }
        }
        if (slot_idx < 0) {
            return make_error(resp_buf, resp_size, 409, "parent_limit_reached");
        }

        s_join_fails = 0;
        s_join_lockout_until_ms = 0;
        parent_slot_t *slot = &s_state.secondary[slot_idx];
        slot->active = true;
        copy_str(slot->parent_id, sizeof(slot->parent_id), parent_id);
        copy_str(slot->label, sizeof(slot->label), label);
        gen_hex_token(slot->token);
        save_state();
        if (out_ui_dirty) *out_ui_dirty = true;

        str_writer_t w;
        sw_init(&w, resp_buf, resp_size);
        sw_raw(&w, "{\"ok\":true,\"role\":\"secondary\",\"token\":");
        sw_json_str(&w, slot->token);
        sw_raw(&w, ",\"parent_count\":");
        sw_uint(&w, parent_count_locked());
        sw_raw(&w, "}");
        return finish_json(&w, resp_buf, resp_size);
    }

    // 以下端点全部需要已绑定家长 token
    bool is_primary = false;
    parent_slot_t *caller = authenticate_locked(auth_header, body, &is_primary);
    if (!caller) {
        return make_error(resp_buf, resp_size, 401, "unauthorized");
    }

    // 4. GET /api/pair/members
    if (is_get && path_is(path, "/api/pair/members")) {
        str_writer_t w;
        sw_init(&w, resp_buf, resp_size);
        sw_raw(&w, "{\"ok\":true,\"parent_count\":");
        sw_uint(&w, parent_count_locked());
        sw_raw(&w, ",\"parents\":[");
        bool first = true;
        if (s_state.primary.active) {
            sw_raw(&w, "{\"parent_id\":");
            sw_json_str(&w, s_state.primary.parent_id);
            sw_raw(&w, ",\"label\":");
            sw_json_str(&w, s_state.primary.label);
            sw_raw(&w, ",\"role\":\"primary\"}");
            first = false;
        }
        for (int i = 0; i < ECARD_ROOM_MAX_SECONDARY_PARENTS; ++i) {
            if (!s_state.secondary[i].active) continue;
            if (!first) sw_raw(&w, ",");
            sw_raw(&w, "{\"parent_id\":");
            sw_json_str(&w, s_state.secondary[i].parent_id);
            sw_raw(&w, ",\"label\":");
            sw_json_str(&w, s_state.secondary[i].label);
            sw_raw(&w, ",\"role\":\"secondary\"}");
            first = false;
        }
        sw_raw(&w, "]}");
        return finish_json(&w, resp_buf, resp_size);
    }

    // 5. GET / POST /api/pair/invite
    if ((is_get || is_post) && path_is(path, "/api/pair/invite")) {
        if (is_post) {
            if (!is_primary) {
                return make_error(resp_buf, resp_size, 403, "primary_only");
            }
            bool rotate = false;
            if ((json_get_bool(body, "rotate", &rotate) && rotate) ||
                !s_state.invite_code[0]) {
                gen_alpha_code(s_state.invite_code, INVITE_CODE_LEN);
                save_state();
            }
        }
        str_writer_t w;
        sw_init(&w, resp_buf, resp_size);
        sw_raw(&w, "{\"ok\":true,\"invite_code\":");
        sw_json_str(&w, s_state.invite_code);
        sw_raw(&w, ",\"device_id\":");
        sw_json_str(&w, dev_id);
        sw_raw(&w, ",\"ip\":");
        sw_json_str(&w, ip_str);
        sw_raw(&w, ",\"port\":");
        sw_uint(&w, port_num);
        sw_raw(&w, ",\"parent_count\":");
        sw_uint(&w, parent_count_locked());
        sw_raw(&w, "}");
        return finish_json(&w, resp_buf, resp_size);
    }

    // 6. POST /api/pair/unbind
    if (is_post && path_is(path, "/api/pair/unbind")) {
        char target[PARENT_ID_MAX];
        const bool has_target = json_get_str(body, "target_parent_id", target, sizeof(target)) &&
                                target[0] != '\0';
        if (!is_primary) {
            if (has_target && strcmp(target, caller->parent_id) != 0) {
                return make_error(resp_buf, resp_size, 403, "forbidden");
            }
            memset(caller, 0, sizeof(*caller));
        } else {
            if (!has_target || strcmp(target, s_state.primary.parent_id) == 0) {
                memset(&s_state.primary, 0, sizeof(s_state.primary));
                memset(s_state.secondary, 0, sizeof(s_state.secondary));
                memset(s_state.invite_code, 0, sizeof(s_state.invite_code));
            } else {
                bool removed = false;
                for (int i = 0; i < ECARD_ROOM_MAX_SECONDARY_PARENTS; ++i) {
                    if (s_state.secondary[i].active &&
                        strcmp(s_state.secondary[i].parent_id, target) == 0) {
                        memset(&s_state.secondary[i], 0, sizeof(s_state.secondary[i]));
                        removed = true;
                        break;
                    }
                }
                if (!removed) {
                    return make_error(resp_buf, resp_size, 404, "parent_not_found");
                }
            }
        }
        save_state();
        if (out_ui_dirty) *out_ui_dirty = true;

        str_writer_t w;
        sw_init(&w, resp_buf, resp_size);
        sw_raw(&w, "{\"ok\":true,\"bound\":");
        sw_raw(&w, s_state.primary.active ? "true" : "false");
        sw_raw(&w, ",\"parent_count\":");
        sw_uint(&w, parent_count_locked());
        sw_raw(&w, "}");
        return finish_json(&w, resp_buf, resp_size);
    }

    // 7. GET /api/cat-room
    if (is_get && path_is(path, "/api/cat-room")) {
        str_writer_t w;
        sw_init(&w, resp_buf, resp_size);
        sw_raw(&w, "{\"ok\":true,\"pending_points\":");
        sw_uint(&w, s_state.pending_points);
        sw_raw(&w, ",\"claimed_points\":");
        sw_uint(&w, s_state.claimed_points);
        sw_raw(&w, ",\"completed_sessions\":");
        sw_uint(&w, s_state.completed_sessions);
        sw_raw(&w, ",\"equipped\":{");
        for (int s = 0; s < ECARD_ROOM_SLOT_COUNT; ++s) {
            if (s > 0) sw_raw(&w, ",");
            sw_json_str(&w, k_slot_names[s]);
            sw_raw(&w, ":");
            const ecard_room_item_t *eq =
                ecard_cat_room_item((ecard_room_slot_t)s, s_state.equipped[s]);
            sw_json_str(&w, eq ? eq->id : "");
        }
        sw_raw(&w, "},\"items\":[");
        for (int i = 0; i < ECARD_ROOM_ITEM_COUNT; ++i) {
            const ecard_room_item_t *it = &k_catalog[i];
            if (i > 0) sw_raw(&w, ",");
            sw_raw(&w, "{\"id\":");
            sw_json_str(&w, it->id);
            sw_raw(&w, ",\"slot\":");
            sw_json_str(&w, k_slot_names[it->slot]);
            sw_raw(&w, ",\"name\":");
            sw_json_str(&w, it->name);
            sw_raw(&w, ",\"cost\":");
            sw_uint(&w, it->cost);
            sw_raw(&w, ",\"unlocked\":");
            sw_raw(&w, is_unlocked_locked(it->slot, it->variant) ? "true" : "false");
            sw_raw(&w, ",\"equipped\":");
            sw_raw(&w, (s_state.equipped[it->slot] == it->variant) ? "true" : "false");
            sw_raw(&w, "}");
        }
        sw_raw(&w, "]}");
        return finish_json(&w, resp_buf, resp_size);
    }

    // 8. POST /api/cat-room/claim
    if (is_post && path_is(path, "/api/cat-room/claim")) {
        const uint16_t delta = s_state.pending_points;
        if (delta > 0) {
            s_state.claimed_points = (uint16_t)(s_state.claimed_points + delta);
            s_state.pending_points = 0;
            save_state();
            if (out_ui_dirty) *out_ui_dirty = true;
        }
        str_writer_t w;
        sw_init(&w, resp_buf, resp_size);
        sw_raw(&w, "{\"ok\":true,\"claimed_delta\":");
        sw_uint(&w, delta);
        sw_raw(&w, ",\"pending_points\":");
        sw_uint(&w, s_state.pending_points);
        sw_raw(&w, ",\"claimed_points\":");
        sw_uint(&w, s_state.claimed_points);
        sw_raw(&w, "}");
        return finish_json(&w, resp_buf, resp_size);
    }

    // 9. POST /api/cat-room/redeem
    if (is_post && path_is(path, "/api/cat-room/redeem")) {
        char item_id[48];
        if (!json_get_str(body, "item_id", item_id, sizeof(item_id))) {
            return make_error(resp_buf, resp_size, 400, "invalid_params");
        }
        const ecard_room_item_t *it = find_item_by_id(item_id);
        if (!it) {
            return make_error(resp_buf, resp_size, 404, "item_not_found");
        }
        if (!is_unlocked_locked(it->slot, it->variant)) {
            if (s_state.claimed_points < it->cost) {
                return make_error(resp_buf, resp_size, 400, "insufficient_points");
            }
            s_state.claimed_points = (uint16_t)(s_state.claimed_points - it->cost);
            s_state.unlocked_mask |= item_bit(it->slot, it->variant);
            save_state();
            if (out_ui_dirty) *out_ui_dirty = true;
        }
        str_writer_t w;
        sw_init(&w, resp_buf, resp_size);
        sw_raw(&w, "{\"ok\":true,\"item_id\":");
        sw_json_str(&w, it->id);
        sw_raw(&w, ",\"unlocked\":true,\"claimed_points\":");
        sw_uint(&w, s_state.claimed_points);
        sw_raw(&w, "}");
        return finish_json(&w, resp_buf, resp_size);
    }

    // 10. POST /api/cat-room/equip
    if (is_post && path_is(path, "/api/cat-room/equip")) {
        char item_id[48];
        char slot_name[24];
        if (!json_get_str(body, "item_id", item_id, sizeof(item_id))) {
            return make_error(resp_buf, resp_size, 400, "invalid_params");
        }
        const ecard_room_item_t *it = find_item_by_id(item_id);
        if (!it) {
            return make_error(resp_buf, resp_size, 404, "item_not_found");
        }
        if (json_get_str(body, "slot", slot_name, sizeof(slot_name)) &&
            slot_name[0] &&
            strcmp(slot_name, k_slot_names[it->slot]) != 0) {
            return make_error(resp_buf, resp_size, 400, "slot_mismatch");
        }
        if (!is_unlocked_locked(it->slot, it->variant)) {
            return make_error(resp_buf, resp_size, 403, "item_locked");
        }
        s_state.equipped[it->slot] = it->variant;
        save_state();
        if (out_ui_dirty) *out_ui_dirty = true;

        str_writer_t w;
        sw_init(&w, resp_buf, resp_size);
        sw_raw(&w, "{\"ok\":true,\"slot\":");
        sw_json_str(&w, k_slot_names[it->slot]);
        sw_raw(&w, ",\"item_id\":");
        sw_json_str(&w, it->id);
        sw_raw(&w, "}");
        return finish_json(&w, resp_buf, resp_size);
    }

    return make_error(resp_buf, resp_size, 404, "not_found");
}

int ecard_cat_room_handle_http(const char *method, const char *path,
                               const char *auth_header, const char *body,
                               const char *device_id, const char *sta_ip,
                               uint16_t port, char *resp_buf, size_t resp_size,
                               bool *out_ui_dirty) {
    room_lock();
    const int status = handle_http_locked(method, path, auth_header, body,
                                          device_id, sta_ip, port,
                                          resp_buf, resp_size, out_ui_dirty);
    room_unlock();
    return status;
}
