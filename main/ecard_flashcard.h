#ifndef ECARD_FLASHCARD_H
#define ECARD_FLASHCARD_H

#include "ecard_cards.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 单个年级卡组的最大张数（数据层每级最多 50 张）。 */
#define ECARD_FLASHCARD_MAX_CARDS 64

/**
 * @brief 载入一个年级的古诗或单词卡组，开始新一轮。
 *
 * @param cards      卡组首地址（来自 ecard_cards_*）
 * @param count      卡组张数
 * @param audio_base 该卡组在全局发音表中的起始下标；古诗传 0
 */
void ecard_flashcard_load(const ecard_card_t *cards, uint8_t count, uint16_t audio_base);

/** 当前卡片；本轮结束后返回 NULL。 */
const ecard_card_t *ecard_flashcard_current(void);

/** 当前卡片在卡组内的下标（0..total-1）；结束后返回 0。 */
uint8_t ecard_flashcard_index(void);

/** 当前卡片在全局发音表中的下标（古诗无发音，返回 0）。 */
uint16_t ecard_flashcard_audio_index(void);

/** 当前卡片是否已翻到背面。 */
bool ecard_flashcard_is_flipped(void);

/** 翻面；本轮结束或已翻面时忽略。 */
void ecard_flashcard_flip(void);

/**
 * @brief 记录当前卡片是否认识
 *
 * 认识：移出队列；不认识：排到队尾，本轮稍后再出现。
 * 只能在已翻面时记录，否则忽略并返回 false。
 *
 * @return true 表示队列里还有下一张；false 表示本轮结束。
 */
bool ecard_flashcard_mark(bool known);

/** 本轮剩余张数。 */
uint8_t ecard_flashcard_remaining(void);

/** 已掌握张数。 */
uint8_t ecard_flashcard_known(void);

/** 当前卡组总数。 */
uint8_t ecard_flashcard_total(void);

/** 本轮是否结束。 */
bool ecard_flashcard_is_finished(void);

#ifdef __cplusplus
}
#endif

#endif // ECARD_FLASHCARD_H
