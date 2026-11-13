#include "ecard_quiz.h"
#include <assert.h>
#include <stdio.h>

static size_t utf8_chars(const char *s) {
    size_t n = 0;
    for (; *s; ++s) {
        if (((unsigned char)*s & 0xC0) != 0x80) ++n;
    }
    return n;
}

static void test_ai_prompts_fit_voice_entry(void) {
    // 小智官方文本入口上限为 16 个字符，超长会被服务端 alert 拒绝。
    ecard_quiz_reset();
    for (uint8_t i = 0; i < ECARD_QUIZ_QUESTION_COUNT; ++i) {
        const ecard_quiz_question_t *q = ecard_quiz_current();
        assert(q != NULL);
        assert(q->ai_prompt != NULL);
        assert(utf8_chars(q->ai_prompt) <= 16);
        for (int step = 0; step < q->correct_index; ++step) ecard_quiz_move(1);
        assert(ecard_quiz_submit());
        ecard_quiz_advance();
    }
    assert(ecard_quiz_is_finished());
}

static void test_reset_and_navigation(void) {
    ecard_quiz_reset();
    assert(ecard_quiz_index() == 0);
    assert(ecard_quiz_selected() == 0);
    assert(ecard_quiz_score() == 0);
    assert(!ecard_quiz_is_answered());
    assert(!ecard_quiz_is_finished());
    assert(ecard_quiz_current() != NULL);

    ecard_quiz_move(1);
    assert(ecard_quiz_selected() == 1);
    ecard_quiz_move(1);
    assert(ecard_quiz_selected() == 2);
    ecard_quiz_move(1);
    assert(ecard_quiz_selected() == 0); // 循环
    ecard_quiz_move(-1);
    assert(ecard_quiz_selected() == 2);
}

static void test_submit_and_feedback(void) {
    ecard_quiz_reset();
    const ecard_quiz_question_t *q = ecard_quiz_current();
    assert(q != NULL);

    // 选中正确答案
    for (int i = 0; i < q->correct_index; ++i) ecard_quiz_move(1);
    assert(ecard_quiz_submit());
    assert(ecard_quiz_is_answered());
    assert(ecard_quiz_score() == 1);

    // 已作答后不能再改选项或重复提交
    ecard_quiz_move(1);
    assert(ecard_quiz_selected() == q->correct_index);
    assert(!ecard_quiz_submit());
    assert(ecard_quiz_score() == 1);
}

static void test_wrong_answer(void) {
    ecard_quiz_reset();
    const ecard_quiz_question_t *q = ecard_quiz_current();
    uint8_t wrong = (uint8_t)((q->correct_index + 1) % ECARD_QUIZ_OPTION_COUNT);
    for (int i = 0; i < wrong; ++i) ecard_quiz_move(1);
    assert(!ecard_quiz_submit());
    assert(ecard_quiz_is_answered());
    assert(ecard_quiz_score() == 0);
}

static void test_full_run_to_result(void) {
    ecard_quiz_reset();
    uint8_t total = ecard_quiz_total();
    assert(total == ECARD_QUIZ_QUESTION_COUNT);

    for (uint8_t i = 0; i < total; ++i) {
        const ecard_quiz_question_t *q = ecard_quiz_current();
        assert(q != NULL);
        assert(!ecard_quiz_is_answered());

        // 全部答对
        for (int step = 0; step < q->correct_index; ++step) ecard_quiz_move(1);
        assert(ecard_quiz_submit());
        bool has_next = ecard_quiz_advance();
        if (i + 1 < total) {
            assert(has_next);
            assert(ecard_quiz_index() == i + 1);
            assert(!ecard_quiz_is_answered());
        } else {
            assert(!has_next);
            assert(ecard_quiz_is_finished());
        }
    }

    assert(ecard_quiz_score() == total);
    assert(ecard_quiz_current() == NULL);
    assert(ecard_quiz_is_finished());
}

static void test_advance_requires_answer(void) {
    ecard_quiz_reset();
    // 未作答时 advance 不推进题号
    assert(ecard_quiz_advance());
    assert(ecard_quiz_index() == 0);
    assert(!ecard_quiz_is_answered());
}

int main(void) {
    printf("Running ecard_quiz tests...\n");
    test_ai_prompts_fit_voice_entry();
    test_reset_and_navigation();
    test_submit_and_feedback();
    test_wrong_answer();
    test_full_run_to_result();
    test_advance_requires_answer();
    printf("ecard_quiz tests passed!\n");
    return 0;
}
