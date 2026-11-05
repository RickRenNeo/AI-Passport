#ifndef ECARD_PROFILE_H
#define ECARD_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ECARD_ROLE_STUDENT = 0,
    ECARD_ROLE_FACULTY = 1,
    ECARD_ROLE_STAFF   = 2
} ecard_role_t;

typedef enum {
    ECARD_WATERMARK_NONE     = 0,
    ECARD_WATERMARK_EMBLEM   = 1, // 校徽微缩暗纹
    ECARD_WATERMARK_STRIPE   = 2, // 极客条纹水印
    ECARD_WATERMARK_GRADE    = 3  // 年级文字水印
} ecard_watermark_t;

/**
 * @brief 身份徽章主题配色方案
 * 方案 A 提供内置出厂配色；方案 B 允许从 NVS / 云端动态覆盖。
 */
typedef struct {
    uint16_t bg_color_rgb565;      // 胶囊背景底色
    uint16_t text_color_rgb565;    // 文字颜色
    uint16_t border_color_rgb565;  // 描边与微光颜色
    ecard_watermark_t watermark;   // 水印类型
    bool is_customized;            // 是否为方案 B 动态自定义配置
} ecard_badge_theme_t;

/**
 * @brief 未读语音留言卡片数据
 */
typedef struct {
    bool has_unread;
    uint8_t duration_sec;
    char sender[32];
    uint32_t timestamp;
} ecard_voice_message_t;

/**
 * @brief eCard 用户身份卡全量档案
 */
typedef struct {
    char school_name[48];       // 学校名称
    char student_name[32];      // 姓名
    char class_name[32];        // 班级 / 院系
    char student_id[24];        // 学号 / 工号
    ecard_role_t role;          // 身份角色
    ecard_badge_theme_t theme;  // 徽章主题
    ecard_voice_message_t voice;// 当前未读消息状态
} ecard_profile_t;

/**
 * @brief 获取当前设备上的个人档案 (只读指针)
 */
const ecard_profile_t *ecard_profile_get(void);

/**
 * @brief 重置为出厂默认学生档案 (方案 A 默认值)
 */
void ecard_profile_init_default(void);

/**
 * @brief 获取角色对应的方案 A 默认主题配色
 */
ecard_badge_theme_t ecard_theme_get_preset_a(ecard_role_t role);

/**
 * @brief 动态设置徽章自定义主题 (方案 B 用户定制支持)
 */
void ecard_profile_set_custom_theme(const ecard_badge_theme_t *custom_theme);

/**
 * @brief 收到新的语音留言 (触发卡片互斥展示)
 */
void ecard_profile_set_unread_voice(const char *sender, uint8_t duration_sec);

/**
 * @brief 标记当前语音留言已读 (触发互斥复原，切回姓名班级)
 */
void ecard_profile_clear_unread_voice(void);

/**
 * @brief 检查当前是否应当互斥显示未读消息卡片
 */
bool ecard_profile_has_unread(void);

#ifdef __cplusplus
}
#endif

#endif // ECARD_PROFILE_H
