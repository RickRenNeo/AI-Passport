#ifndef ECARD_CARDS_H
#define ECARD_CARDS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ECARD_GRADE_COUNT 6

/** 卡组类型：古诗接句 / 英语单词。 */
typedef enum {
    ECARD_CARD_POEM = 0,
    ECARD_CARD_WORD
} ecard_card_kind_t;

/**
 * @brief 一张闪卡的全部文本字段。
 *
 * 古诗卡：front/back 为上下句，meta 为「诗题 · 作者」，explain 为一句白话释义。
 * 单词卡：front 为英文，back 为中文词义，example/example_cn 为例句与翻译。
 */
typedef struct {
    const char *front;
    const char *back;
    const char *meta;
    const char *explain;
    const char *example;
    const char *example_cn;
} ecard_card_t;

/**
 * @brief 取指定年级的古诗卡组。
 *
 * @param grade 1..ECARD_GRADE_COUNT
 * @param count 输出该年级的张数，可为 NULL
 * @return 卡组首地址；年级越界返回 NULL
 */
const ecard_card_t *ecard_cards_poems(uint8_t grade, uint8_t *count);

/**
 * @brief 取指定年级的英语单词卡组。
 */
const ecard_card_t *ecard_cards_words(uint8_t grade, uint8_t *count);

/**
 * @brief 指定年级单词卡在全局发音表中的起始下标。
 */
uint16_t ecard_cards_word_offset(uint8_t grade);

/** 全部年级的单词总数（发音表长度）。 */
uint16_t ecard_cards_word_total(void);

#ifdef __cplusplus
}
#endif

#endif // ECARD_CARDS_H
