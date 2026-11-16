#ifndef ECARD_CAT_ROOM_H
#define ECARD_CAT_ROOM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ECARD_ROOM_SLOT_WALL = 0,
    ECARD_ROOM_SLOT_WALL_DECOR,
    ECARD_ROOM_SLOT_CHAIR,
    ECARD_ROOM_SLOT_LAMP,
    ECARD_ROOM_SLOT_DESK_ITEM,
    ECARD_ROOM_SLOT_RUG,
    ECARD_ROOM_SLOT_COUNT
} ecard_room_slot_t;

#define ECARD_ROOM_VARIANTS_PER_SLOT 4
#define ECARD_ROOM_ITEM_COUNT (ECARD_ROOM_SLOT_COUNT * ECARD_ROOM_VARIANTS_PER_SLOT)
#define ECARD_ROOM_MAX_SECONDARY_PARENTS 4

typedef struct {
    const char *id;
    ecard_room_slot_t slot;
    uint8_t variant;
    const char *name;
    uint16_t cost;
} ecard_room_item_t;

void ecard_cat_room_init(void);
void ecard_cat_room_reset_all(void);

uint16_t ecard_cat_room_award_focus_minutes(uint8_t focus_minutes);
uint16_t ecard_cat_room_pending_points(void);
uint16_t ecard_cat_room_claimed_points(void);
uint32_t ecard_cat_room_completed_sessions(void);

uint8_t ecard_cat_room_equipped(ecard_room_slot_t slot);
bool ecard_cat_room_is_unlocked(ecard_room_slot_t slot, uint8_t variant);
const ecard_room_item_t *ecard_cat_room_item(ecard_room_slot_t slot, uint8_t variant);
const char *ecard_cat_room_slot_name(ecard_room_slot_t slot);

bool ecard_cat_room_is_bound(void);
uint8_t ecard_cat_room_parent_count(void);
void ecard_cat_room_get_pair_code(char *out, size_t out_size);
void ecard_cat_room_rotate_pair_code(void);
void ecard_cat_room_set_pairing_active(bool active);
bool ecard_cat_room_is_pairing_active(void);
bool ecard_cat_room_build_qr_payload(const char *device_id, const char *sta_ip,
                                     uint16_t port, char *out, size_t out_size);
#if !defined(ESP_PLATFORM)
void ecard_cat_room_set_now_ms(uint64_t now_ms);
#endif

/**
 * @brief 处理来自 STA 局域网 HTTP 服务的配对与小猫书房 API 请求。
 *
 * @return HTTP 状态码 (200 / 400 / 401 / 403 / 404 / 409 / 429 / 500)
 */
int ecard_cat_room_handle_http(const char *method, const char *path,
                               const char *auth_header, const char *body,
                               const char *device_id, const char *sta_ip,
                               uint16_t port, char *resp_buf, size_t resp_size,
                               bool *out_ui_dirty);

#ifdef __cplusplus
}
#endif

#endif // ECARD_CAT_ROOM_H
