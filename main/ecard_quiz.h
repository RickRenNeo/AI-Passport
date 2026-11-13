#ifndef ECARD_QUIZ_H
#define ECARD_QUIZ_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ECARD_QUIZ_OPTION_COUNT 3
#define ECARD_QUIZ_QUESTION_COUNT 10

typedef struct {
    const char *prompt;
    const char *options[ECARD_QUIZ_OPTION_COUNT];
    uint8_t correct_index;
    /* 小智文本入口最多接受 16 个字符，这里是给语音讲解用的压缩题干。 */
    const char *ai_prompt;
} ecard_quiz_question_t;

/** 回到第一题，清除选择、作答与得分状态。 */
void ecard_quiz_reset(void);

/** 当前题号（0 起始）与总题数。 */
uint8_t ecard_quiz_index(void);
uint8_t ecard_quiz_total(void);

/** 当前选项（0=A，1=B，2=C）与累计答对题数。 */
uint8_t ecard_quiz_selected(void);
uint8_t ecard_quiz_score(void);

bool ecard_quiz_is_answered(void);
bool ecard_quiz_is_finished(void);

/** 当前题目；练习结束后返回 NULL。 */
const ecard_quiz_question_t *ecard_quiz_current(void);

/** 在 A/B/C 之间循环移动；已作答或已结束时忽略。 */
void ecard_quiz_move(int delta);

/** 提交当前选项；已作答或已结束时忽略并返回 false。 */
bool ecard_quiz_submit(void);

/**
 * @brief 进入下一题；未作答时忽略。
 * @return true 仍有下一题；false 表示整套练习结束。
 */
bool ecard_quiz_advance(void);

#ifdef __cplusplus
}
#endif

#endif // ECARD_QUIZ_H
