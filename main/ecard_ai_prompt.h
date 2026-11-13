#ifndef ECARD_AI_PROMPT_H
#define ECARD_AI_PROMPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ecard_cards.h"
#include "ecard_quiz.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 小智服务端对 listen/detect 文本有硬上限：超长会被直接拒绝并返回
 * "Detect is only for wake words, do not send long texts."。
 * 实测（真机 + 官方服务端逐条验证）：
 *   - 含中文的提示词：16 个字符内稳定，18 个字符起被拒；
 *   - 纯英文（单词、例句）：30 个字符内稳定，36 个字符起被拒。
 * 所有卡片提示词都必须在这个上限内构造，不能靠发送时截断——截断会把
 * 题意或例句切坏，学生听到的讲解就会跑偏。
 */
#define ECARD_AI_CJK_MAX_CHARS   16
#define ECARD_AI_ASCII_MAX_BYTES 30

/** UTF-8 字符数（非字节数）。 */
size_t ecard_ai_utf8_chars(const char *text);

/** 文本是否含非 ASCII 字节。 */
bool ecard_ai_has_cjk(const char *text);

/** 该文本适用的字符上限（按内容自动选择中文/英文上限）。 */
size_t ecard_ai_prompt_limit(const char *text);

/** 文本是否在上限内。 */
bool ecard_ai_prompt_fits(const char *text);

/** 按 UTF-8 边界把文本截到上限内（仅作为兜底，正常构造不应触发）。 */
void ecard_ai_prompt_trim(char *text);

/**
 * @brief 构造讲题提示词。
 *
 * 题库里的 ai_prompt 都按 16 字符设计；超长时回退到"讲题"两个字，
 * 保证仍有可用提示词，而不是发一个被服务端拒绝的半截题。
 */
void ecard_ai_build_quiz_prompt(const ecard_quiz_question_t *question,
                                char *out, size_t out_size);

/**
 * @brief 构造古诗提示词：卡片上下句用"|"分隔（两句合计 <=16 字）。
 */
void ecard_ai_build_poem_prompt(const ecard_card_t *card, char *out, size_t out_size);

/**
 * @brief 构造单词卡两轮提示词。
 *
 * first 为"读:单词,词义"（中文，<=16 字符）；example 为卡片原句
 * （纯英文，<=30 字节）。例句过长或含中文时 example 为空串，此时只发第一轮。
 *
 * @param first       第一轮提示词输出缓冲
 * @param first_size  第一轮缓冲大小
 * @param example     第二轮（例句）输出缓冲
 * @param example_size 第二轮缓冲大小
 */
void ecard_ai_build_word_prompts(const ecard_card_t *card,
                                 char *first, size_t first_size,
                                 char *example, size_t example_size);

#ifdef __cplusplus
}
#endif

#endif // ECARD_AI_PROMPT_H
