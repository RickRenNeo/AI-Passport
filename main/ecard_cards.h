#ifndef ECARD_CARDS_H
#define ECARD_CARDS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ECARD_GRADE_COUNT 6
#define ECARD_MAX_ACTIVE_GRADES 3
#define ECARD_MAX_DECKS_PER_GRADE 16
#define ECARD_MAX_CARDS_PER_DECK 24

/** 卡组类型：古诗古文 / 英语单词。 */
typedef enum {
    ECARD_CARD_POEM = 0,
    ECARD_CARD_WORD
} ecard_card_kind_t;

/**
 * @brief 一张闪卡的全部文本字段。
 *
 * 古诗/古文卡：front/back 为上下句，meta 为「篇名 · 作者」，explain 为一句白话释义。
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

/** 单个单元/分组的摘要元数据（供二级选择页展示）。 */
typedef struct {
    char title[48];
    char subtitle[52];
    uint8_t card_count;
} ecard_deck_info_t;

/** 重置内存中的年级配置与挂载路径（默认当前 3 年级，保留 [2, 3, 4] 年级）。 */
void ecard_cards_reset_curriculum(void);

/** 从班级字符串（如 "三年级(2)班"、"五(1)班"、"602班"、"G4"）提取年级 1..6；无法识别返回 0。 */
uint8_t ecard_cards_parse_grade_from_class(const char *class_name);

/** 计算指定当前年级默认保留的 3 个年级窗口（边界自动夹紧为 [1,2,3] .. [4,5,6]）。 */
void ecard_cards_default_grades_for(uint8_t current_grade,
                                    uint8_t out_grades[ECARD_MAX_ACTIVE_GRADES],
                                    uint8_t *out_count);

/**
 * @brief 设置当前学生年级与设备保留年级列表（最多保留 3 个年级）。
 *
 * 若 grades 为 NULL 或 grade_count == 0，则自动按 current_grade 推导默认前后各一年窗口。
 * 若已挂载存储目录，会同步解压新选中的 cur_g<1..6>.pak 并物理删除未选中年级的题库包。
 */
bool ecard_cards_set_curriculum(uint8_t current_grade,
                                const uint8_t *grades,
                                uint8_t grade_count);

/**
 * @brief 挂载设备题库目录（如 "/spiffs"）并按当前配置同步保留最多 3 个年级的 .pak 文件。
 *
 * 传 NULL 或空串可解除挂载（回退为宿主机内存直读模式）。
 */
bool ecard_cards_mount_dir(const char *dir_path);

/** 当前学生所在年级（1..6）。 */
uint8_t ecard_cards_current_grade(void);

/** 读取当前设备保留的年级列表（升序，返回保留个数 1..3）。 */
uint8_t ecard_cards_active_grades(uint8_t out_grades[ECARD_MAX_ACTIVE_GRADES]);

/** 判断指定年级（1..6）是否在当前设备保留列表中。 */
bool ecard_cards_is_grade_active(uint8_t grade);

/** 指定年级下古诗分组数（2..3）或英语单元数（12）。 */
uint8_t ecard_cards_deck_count(ecard_card_kind_t kind, uint8_t grade);

/** 指定年级下古诗/古文总卡数或英语总词数。 */
uint16_t ecard_cards_grade_card_total(ecard_card_kind_t kind, uint8_t grade);

/** 读取指定年级、指定分组/单元的标题与卡片张数摘要。 */
bool ecard_cards_deck_info(ecard_card_kind_t kind, uint8_t grade,
                           uint8_t deck_index, ecard_deck_info_t *out_info);

/**
 * @brief 按需加载指定年级、指定单元/分组的闪卡到静态活动缓冲区。
 *
 * 已挂载存储目录时仅允许加载保留的 <=3 个年级（直接从 cur_g<grade>.pak 读取单组）；
 * 未保留年级返回 NULL。
 */
const ecard_card_t *ecard_cards_load_deck(ecard_card_kind_t kind, uint8_t grade,
                                          uint8_t deck_index, uint8_t *out_count);

/** 指定年级、指定英语单元在全局词表中的起始下标。 */
uint16_t ecard_cards_word_deck_offset(uint8_t grade, uint8_t deck_index);

/** 兼容入口：取指定年级第 1 组古诗卡。 */
const ecard_card_t *ecard_cards_poems(uint8_t grade, uint8_t *count);

/** 兼容入口：取指定年级 Unit 1 英语单词卡。 */
const ecard_card_t *ecard_cards_words(uint8_t grade, uint8_t *count);

/** 指定年级 Unit 1 在全局词表中的起始下标。 */
uint16_t ecard_cards_word_offset(uint8_t grade);

/** 全部 6 个年级的英语单词总数。 */
uint16_t ecard_cards_word_total(void);

#ifdef __cplusplus
}
#endif

#endif // ECARD_CARDS_H
