/*
 * 小智提示词长度契约测试。
 *
 * 服务端对 detect 文本超长会直接拒绝（"Detect is only for wake words..."），
 * 这里遍历全部题库与卡组，保证设备真的能发出的每条提示词都在上限内。
 */

#include "ecard_ai_prompt.h"

#include "ecard_cards.h"
#include "ecard_quiz.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_limits_detected_by_content(void) {
    assert(ecard_ai_prompt_limit("讲题:8+7=?") == ECARD_AI_CJK_MAX_CHARS);
    assert(ecard_ai_prompt_limit("I think about my future.") == ECARD_AI_ASCII_MAX_BYTES);
    assert(!ecard_ai_has_cjk("future"));
    assert(ecard_ai_has_cjk("未来"));

    assert(ecard_ai_prompt_fits("寒雨连江夜入吴|平明送客楚山孤")); // 15 字
    assert(!ecard_ai_prompt_fits("寒雨连江夜入吴平明送客楚山孤洛阳亲友")); // 18 字，实测被拒
    assert(ecard_ai_prompt_fits("I think about my future."));   // 25 字节
    assert(!ecard_ai_prompt_fits("My ruler is twenty centimetres long.")); // 36 字节，实测被拒
    assert(!ecard_ai_prompt_fits(""));
}

static void test_trim_keeps_utf8_boundaries(void) {
    char buf[64];
    snprintf(buf, sizeof(buf), "寒雨连江夜入吴平明送客楚山孤洛阳亲友如相问");
    ecard_ai_prompt_trim(buf);
    assert(ecard_ai_utf8_chars(buf) == ECARD_AI_CJK_MAX_CHARS);
    /* 纯汉字文本每个字 3 字节，截断后字节数必须仍是 3 的倍数，
     * 否则说明切在了 UTF-8 中间。 */
    assert(strlen(buf) % 3 == 0);

    snprintf(buf, sizeof(buf), "My ruler is twenty centimetres long.");
    ecard_ai_prompt_trim(buf);
    assert(strlen(buf) == ECARD_AI_ASCII_MAX_BYTES);
}

static void test_every_quiz_prompt_fits(void) {
    ecard_quiz_reset();
    for (uint8_t i = 0; i < ECARD_QUIZ_QUESTION_COUNT; ++i) {
        const ecard_quiz_question_t *q = ecard_quiz_current();
        assert(q != NULL);
        char prompt[96];
        ecard_ai_build_quiz_prompt(q, prompt, sizeof(prompt));
        assert(prompt[0] != '\0');
        assert(ecard_ai_prompt_fits(prompt));
        if (!ecard_quiz_advance()) {
            /* 未作答时 advance 被忽略，这里手动推进避免死循环。 */
            break;
        }
    }
}

static void test_every_poem_pair_fits(void) {
    for (uint8_t grade = 1; grade <= ECARD_GRADE_COUNT; ++grade) {
        uint8_t count = 0;
        const ecard_card_t *cards = ecard_cards_poems(grade, &count);
        assert(cards != NULL);
        for (uint8_t i = 0; i < count; ++i) {
            char prompt[96];
            ecard_ai_build_poem_prompt(&cards[i], prompt, sizeof(prompt));
            assert(prompt[0] != '\0');
            assert(ecard_ai_prompt_fits(prompt));
            /* 正常情况下应该是上下句原样拼起来，不该退化。 */
            if (cards[i].front && cards[i].back) {
                char pair[64];
                snprintf(pair, sizeof(pair), "%s|%s", cards[i].front, cards[i].back);
                assert(strcmp(prompt, pair) == 0);
            }
        }
    }
}

static void test_every_word_prompt_fits(void) {
    for (uint8_t grade = 1; grade <= ECARD_GRADE_COUNT; ++grade) {
        uint8_t count = 0;
        const ecard_card_t *cards = ecard_cards_words(grade, &count);
        assert(cards != NULL);
        for (uint8_t i = 0; i < count; ++i) {
            char first[96];
            char example[64];
            ecard_ai_build_word_prompts(&cards[i], first, sizeof(first),
                                        example, sizeof(example));
            assert(first[0] != '\0');
            assert(ecard_ai_prompt_fits(first));
            assert(strncmp(first, "读:", 3) == 0);
            if (example[0]) {
                assert(ecard_ai_prompt_fits(example));
                assert(strcmp(example, cards[i].example) == 0);
            }
        }
    }
}

static void test_word_without_example_still_reads_word(void) {
    ecard_card_t card = {
        .front = "future",
        .back = "未来",
        .example = NULL,
        .example_cn = NULL,
    };
    char first[96];
    char example[64];
    ecard_ai_build_word_prompts(&card, first, sizeof(first), example, sizeof(example));
    assert(strcmp(first, "读:future,未来") == 0);
    assert(example[0] == '\0');
}

static void test_long_meaning_falls_back_to_word_only(void) {
    ecard_card_t card = {
        .front = "present",
        .back = "现在；礼物；出席的",
        .example = "We should enjoy the present.",
    };
    char first[96];
    char example[64];
    ecard_ai_build_word_prompts(&card, first, sizeof(first), example, sizeof(example));
    assert(strcmp(first, "读:present") == 0);       /* 16 字内装不下完整词义 */
    assert(strcmp(example, "We should enjoy the present.") == 0);
}

int main(void) {
    test_limits_detected_by_content();
    test_trim_keeps_utf8_boundaries();
    test_every_quiz_prompt_fits();
    test_every_poem_pair_fits();
    test_every_word_prompt_fits();
    test_word_without_example_still_reads_word();
    test_long_meaning_falls_back_to_word_only();
    printf("test_ecard_ai_prompt: OK\n");
    return 0;
}
