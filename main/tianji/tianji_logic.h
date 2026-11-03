// main/tianji_logic.h — 天机·六爻: 六爻起卦与六十四卦逻辑引擎（纯逻辑，解耦于平台）
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 爻类型 (三钱法结果)
typedef enum {
    TIANJI_LINE_LAOYIN  = 6,  // 老阴（三背/0正3背）：变阳爻 (━━ ━━ ✖)
    TIANJI_LINE_SHAOYANG = 7, // 少阳（二正一背）：静阳爻 (━━━━━)
    TIANJI_LINE_SHAOYIN  = 8, // 少阴（一正二背）：静阴爻 (━━ ━━)
    TIANJI_LINE_LAOYANG = 9,  // 老阳（三正/3正0背）：变阴爻 (━━━━━ ●)
} tianji_line_type_t;

// 八卦基础代码 (0..7, 3-bit: 初爻LSB, 三爻MSB)
// 0(000): 坤(地), 1(001): 震(雷), 2(010): 坎(水), 3(011): 兑(泽)
// 4(100): 艮(山), 5(101): 离(火), 6(110): 巽(风), 7(111): 乾(天)
typedef enum {
    TIANJI_TRIGRAM_KUN  = 0, // 坤 ☷ 地
    TIANJI_TRIGRAM_ZHEN = 1, // 震 ☳ 雷
    TIANJI_TRIGRAM_KAN  = 2, // 坎 ☵ 水
    TIANJI_TRIGRAM_DUI  = 3, // 兑 ☱ 泽
    TIANJI_TRIGRAM_GEN  = 4, // 艮 ☶ 山
    TIANJI_TRIGRAM_LI   = 5, // 离 ☲ 火
    TIANJI_TRIGRAM_XUN  = 6, // 巽 ☴ 风
    TIANJI_TRIGRAM_QIAN = 7, // 乾 ☰ 天
} tianji_trigram_t;

// 卦象信息结构
typedef struct {
    uint8_t index;              // 序号 1..64
    uint8_t bits;               // 6-bit 编码 (bit0:初爻 ... bit5:上爻, 1=阳, 0=阴)
    const char *name;           // 完整卦名（如 "火天大有"）
    const char *pinyin_name;    // 带生僻拼音的卦名（如 "火雷噬嗑（shì kè）"）
    const char *short_name;     // 卦简称（如 "大有"）
    const char *upper_name;     // 上卦（如 "离"）
    const char *lower_name;     // 下卦（如 "乾"）
    const char *judgment;       // 卦辞
    const char *xiang;          // 大象/白话象曰
    const char *career;         // 事业断语
    const char *wealth;         // 财运断语
    const char *relationship;   // 姻缘断语
} tianji_hexagram_t;

// 单次掷硬币结果
typedef struct {
    uint8_t coins[3];           // 3枚硬币正反: 1=字(正面/阳=3分), 0=背(背面/阴=2分)
    uint8_t sum_score;          // 和值 (6, 7, 8, 9)
    tianji_line_type_t type;    // 爻性质
} tianji_toss_result_t;

// 六爻起卦会话状态
typedef struct {
    uint8_t current_line;       // 当前已掷出的爻数 0..6 (0表示未开始, 6表示已完成)
    tianji_toss_result_t toss_history[6]; // 初爻(0) 到 上爻(5) 的掷卦记录
    tianji_line_type_t lines[6];// 6个爻记录
    uint8_t primary_bits;       // 本卦 6-bit
    uint8_t transformed_bits;   // 变卦 6-bit
    uint8_t moving_count;       // 动爻数量 (0..6)
    uint8_t moving_indices[6];  // 动爻位置索引 (0..5)
    bool is_completed;          // 是否起卦完毕
} tianji_session_t;

// 初始化起卦会话
void tianji_session_init(tianji_session_t *session);

// 单次抛掷硬币生成当前爻 (自底向上, 初爻->上爻)
// rand_seed_func: 可选外部随机数源, 若为 NULL 则使用内部 rand()
bool tianji_session_cast_step(tianji_session_t *session, uint32_t (*rand_func)(void), tianji_toss_result_t *out_toss);

// 直接手动注入一爻 (主要用于单元测试与确定性验证)
bool tianji_session_inject_line(tianji_session_t *session, tianji_line_type_t line_type);

// 查表检索指定 6-bit 对应的卦象
const tianji_hexagram_t *tianji_get_hexagram(uint8_t bits);

// 根据序号 1..64 获取卦象
const tianji_hexagram_t *tianji_get_hexagram_by_index(uint8_t index);

// 工具函数: 判断某爻是否为阳爻 (1=阳, 0=阴)
bool tianji_line_is_yang(tianji_line_type_t type);

// 工具函数: 判断某爻是否为动爻
bool tianji_line_is_moving(tianji_line_type_t type);

// 工具函数: 变爻转化后的新爻
bool tianji_line_transformed_is_yang(tianji_line_type_t type);

// 工具函数: 获取八卦名称
const char *tianji_trigram_name(tianji_trigram_t tri);

// 工具函数: 获取八卦自然象征
const char *tianji_trigram_nature(tianji_trigram_t tri);

#ifdef __cplusplus
}
#endif
