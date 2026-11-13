#include "ecard_flashcard.h"
#include <stddef.h>
#include <string.h>

/* 待学队列只存卡组内下标，不认识就移到队尾，容量恒为当前卡组张数。 */
static const ecard_card_t *s_cards;
static uint16_t s_audio_base;
static uint8_t s_pending[ECARD_FLASHCARD_MAX_CARDS];
static uint8_t s_pending_count;
static uint8_t s_total;
static bool s_flipped;

void ecard_flashcard_load(const ecard_card_t *cards, uint8_t count, uint16_t audio_base) {
    s_cards = cards;
    s_audio_base = audio_base;
    s_flipped = false;

    if (!cards || count == 0) {
        s_total = 0;
        s_pending_count = 0;
        return;
    }
    if (count > ECARD_FLASHCARD_MAX_CARDS) count = ECARD_FLASHCARD_MAX_CARDS;

    s_total = count;
    for (uint8_t i = 0; i < count; ++i) {
        s_pending[i] = i;
    }
    s_pending_count = count;
}

const ecard_card_t *ecard_flashcard_current(void) {
    if (s_pending_count == 0 || !s_cards) return NULL;
    return &s_cards[s_pending[0]];
}

uint8_t ecard_flashcard_index(void) {
    if (s_pending_count == 0) return 0;
    return s_pending[0];
}

uint16_t ecard_flashcard_audio_index(void) {
    if (s_pending_count == 0 || s_audio_base == 0) return 0;
    return (uint16_t)(s_audio_base + s_pending[0]);
}

bool ecard_flashcard_is_flipped(void) {
    return s_flipped;
}

void ecard_flashcard_flip(void) {
    if (s_pending_count == 0) return;
    s_flipped = true;
}

bool ecard_flashcard_mark(bool known) {
    if (s_pending_count == 0 || !s_flipped) return false;

    if (known) {
        for (uint8_t i = 1; i < s_pending_count; ++i) {
            s_pending[i - 1] = s_pending[i];
        }
        --s_pending_count;
    } else {
        // 不认识：队首移到队尾，本轮稍后再考一次
        uint8_t first = s_pending[0];
        for (uint8_t i = 1; i < s_pending_count; ++i) {
            s_pending[i - 1] = s_pending[i];
        }
        s_pending[s_pending_count - 1] = first;
    }

    s_flipped = false;
    return s_pending_count > 0;
}

uint8_t ecard_flashcard_remaining(void) {
    return s_pending_count;
}

uint8_t ecard_flashcard_known(void) {
    return (uint8_t)(s_total - s_pending_count);
}

uint8_t ecard_flashcard_total(void) {
    return s_total;
}

bool ecard_flashcard_is_finished(void) {
    return s_total > 0 && s_pending_count == 0;
}
