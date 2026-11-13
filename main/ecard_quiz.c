#include "ecard_quiz.h"
#include <stddef.h>

/* 一年级综合练习：语文 / 数学 / 英语，含长题干与长选项的排版样例。 */
static const ecard_quiz_question_t k_questions[ECARD_QUIZ_QUESTION_COUNT] = {
    {
        .prompt = "“太阳”的拼音是？",
        .options = {"tài yáng", "dà yáng", "tài yán"},
        .correct_index = 0,
        .ai_prompt = "太阳的拼音怎么读",
    },
    {
        .prompt = "8 + 7 = ?",
        .options = {"13", "15", "16"},
        .correct_index = 1,
        .ai_prompt = "讲题:8+7=?",
    },
    {
        .prompt = "15 - 6 = ?",
        .options = {"7", "8", "9"},
        .correct_index = 2,
        .ai_prompt = "讲题:15-6=?",
    },
    {
        .prompt = "“春眠不觉晓”的下一句是？",
        .options = {"处处闻啼鸟", "夜来风雨声", "花落知多少"},
        .correct_index = 0,
        .ai_prompt = "讲题:春眠不觉晓下一句",
    },
    {
        .prompt = "“苹果”的英文是？",
        .options = {"apple", "banana", "orange"},
        .correct_index = 0,
        .ai_prompt = "讲题:苹果英文",
    },
    {
        .prompt = "小明有 12 颗糖，分给同学 5 颗后，还剩几颗？",
        .options = {"5 颗", "6 颗", "7 颗"},
        .correct_index = 2,
        .ai_prompt = "讲题:12颗糖分5颗剩几",
    },
    {
        .prompt = "《静夜思》中“举头望明月”的下一句是？",
        .options = {"低头思故乡", "疑是地上霜", "床前明月光"},
        .correct_index = 0,
        .ai_prompt = "讲题:静夜思举头望明月下句",
    },
    {
        .prompt = "小华从家到学校要走 15 分钟，他早上 7:40 出发，路上没有停留，几时到达学校？",
        .options = {"7:55", "8:00", "8:05"},
        .correct_index = 0,
        .ai_prompt = "讲题:15分钟7点40出发几时到",
    },
    {
        .prompt = "20 里面有几个 5？",
        .options = {"3", "4", "5"},
        .correct_index = 1,
        .ai_prompt = "讲题:20里面有几个5",
    },
    {
        .prompt = "下面哪一句描写春天？",
        .options = {"春眠不觉晓，处处闻啼鸟", "白日依山尽，黄河入海流",
                    "举头望明月，低头思故乡"},
        .correct_index = 0,
        .ai_prompt = "讲题:哪句写春天春眠不觉晓",
    },
};

_Static_assert(sizeof(k_questions) / sizeof(k_questions[0]) == ECARD_QUIZ_QUESTION_COUNT,
               "question bank size must match ECARD_QUIZ_QUESTION_COUNT");

static uint8_t s_index;
static uint8_t s_selected;
static uint8_t s_score;
static bool s_answered;
static bool s_finished;

void ecard_quiz_reset(void) {
    s_index = 0;
    s_selected = 0;
    s_score = 0;
    s_answered = false;
    s_finished = false;
}

uint8_t ecard_quiz_index(void) {
    return s_index;
}

uint8_t ecard_quiz_total(void) {
    return ECARD_QUIZ_QUESTION_COUNT;
}

uint8_t ecard_quiz_selected(void) {
    return s_selected;
}

uint8_t ecard_quiz_score(void) {
    return s_score;
}

bool ecard_quiz_is_answered(void) {
    return s_answered;
}

bool ecard_quiz_is_finished(void) {
    return s_finished;
}

const ecard_quiz_question_t *ecard_quiz_current(void) {
    if (s_finished || s_index >= ECARD_QUIZ_QUESTION_COUNT) return NULL;
    return &k_questions[s_index];
}

void ecard_quiz_move(int delta) {
    if (s_answered || s_finished) return;
    int next = ((int)s_selected + delta) % ECARD_QUIZ_OPTION_COUNT;
    if (next < 0) next += ECARD_QUIZ_OPTION_COUNT;
    s_selected = (uint8_t)next;
}

bool ecard_quiz_submit(void) {
    const ecard_quiz_question_t *q = ecard_quiz_current();
    if (!q || s_answered) return false;

    s_answered = true;
    if (s_selected == q->correct_index) {
        ++s_score;
        return true;
    }
    return false;
}

bool ecard_quiz_advance(void) {
    if (!s_answered) return true;

    ++s_index;
    s_selected = 0;
    s_answered = false;
    if (s_index >= ECARD_QUIZ_QUESTION_COUNT) {
        s_finished = true;
        return false;
    }
    return true;
}
