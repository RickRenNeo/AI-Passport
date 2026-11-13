// main/ecard_ai_prompt.c —— 小智文本提示词的长度约束与构造
//
// 这里的每条规则都来自真机 + 官方服务端实测：服务端对 detect 文本超长
// 会直接回 alert 并丢弃整轮对话，所以提示词必须在“卡片数据”层面就
// 保证长度合规，而不是发送前才截断。

#include "ecard_ai_prompt.h"

#include <stdio.h>
#include <string.h>

size_t ecard_ai_utf8_chars(const char *text) {
    size_t n = 0;
    if (!text) return 0;
    for (const char *p = text; *p; ++p) {
        if (((unsigned char)*p & 0xC0) != 0x80) ++n;
    }
    return n;
}

bool ecard_ai_has_cjk(const char *text) {
    if (!text) return false;
    for (const char *p = text; *p; ++p) {
        if ((unsigned char)*p >= 0x80) return true;
    }
    return false;
}

size_t ecard_ai_prompt_limit(const char *text) {
    return ecard_ai_has_cjk(text) ? ECARD_AI_CJK_MAX_CHARS : ECARD_AI_ASCII_MAX_BYTES;
}

bool ecard_ai_prompt_fits(const char *text) {
    if (!text || !text[0]) return false;
    if (ecard_ai_has_cjk(text)) {
        return ecard_ai_utf8_chars(text) <= ECARD_AI_CJK_MAX_CHARS;
    }
    return strlen(text) <= ECARD_AI_ASCII_MAX_BYTES;
}

void ecard_ai_prompt_trim(char *text) {
    if (!text) return;
    if (!ecard_ai_has_cjk(text)) {
        const size_t len = strlen(text);
        if (len > ECARD_AI_ASCII_MAX_BYTES) text[ECARD_AI_ASCII_MAX_BYTES] = '\0';
        return;
    }
    size_t chars = 0;
    for (char *p = text; *p; ++p) {
        if (((unsigned char)*p & 0xC0) == 0x80) continue;
        if (++chars > ECARD_AI_CJK_MAX_CHARS) {
            *p = '\0';
            return;
        }
    }
}

void ecard_ai_build_quiz_prompt(const ecard_quiz_question_t *question,
                                char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!question) return;
    const char *prompt = question->ai_prompt ? question->ai_prompt : "讲题";
    if (ecard_ai_prompt_fits(prompt)) {
        snprintf(out, out_size, "%s", prompt);
        return;
    }
    // 题库设计值都在 16 字内；万一有新题超长，宁可退化成通用讲题提示，
    // 也不要发半截题被服务端拒绝（学生至少还能听到方法讲解）。
    snprintf(out, out_size, "讲题");
}

void ecard_ai_build_poem_prompt(const ecard_card_t *card, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!card) return;

    if (card->front && card->back && card->front[0] && card->back[0]) {
        char pair[64];
        snprintf(pair, sizeof(pair), "%s|%s", card->front, card->back);
        if (ecard_ai_prompt_fits(pair)) {
            snprintf(out, out_size, "%s", pair);
            return;
        }
    }
    // 上下句拼起来超长时退回“诗题 · 作者”让模型自己找诗，
    // 再不行就只用上句。
    if (card->meta && card->meta[0]) {
        char meta[64];
        snprintf(meta, sizeof(meta), "读诗:%s", card->meta);
        ecard_ai_prompt_trim(meta);
        snprintf(out, out_size, "%s", meta);
        return;
    }
    snprintf(out, out_size, "读诗:%s", card->front ? card->front : "");
    ecard_ai_prompt_trim(out);
}

void ecard_ai_build_word_prompts(const ecard_card_t *card,
                                 char *first, size_t first_size,
                                 char *example, size_t example_size) {
    if (first && first_size) first[0] = '\0';
    if (example && example_size) example[0] = '\0';
    if (!card || !first || !first_size) return;

    char with_meaning[96];
    snprintf(with_meaning, sizeof(with_meaning), "读:%s,%s",
             card->front ? card->front : "", card->back ? card->back : "");
    if (ecard_ai_prompt_fits(with_meaning)) {
        snprintf(first, first_size, "%s", with_meaning);
    } else {
        // 词义太长（如 present,现在；礼物）就退化成只读单词，让模型自己解释词义，
        // 绝不截断成“读:present,现在；礼”这种半截词义。
        snprintf(first, first_size, "读:%s", card->front ? card->front : "");
        ecard_ai_prompt_trim(first);
    }

    if (!example || !example_size) return;
    if (!card->example || !card->example[0]) return;
    if (ecard_ai_has_cjk(card->example)) return;
    if (strlen(card->example) > ECARD_AI_ASCII_MAX_BYTES) return;
    snprintf(example, example_size, "%s", card->example);
}
