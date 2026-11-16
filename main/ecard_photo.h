#ifndef ECARD_PHOTO_H
#define ECARD_PHOTO_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 首页大证件照 (130 × 184, RGB565)，嵌入 134 × 188 相框。
 *
 * 由 tools/generate_ecard_photo.py 从 assets/images/ecard_portrait_source.jpg
 * 生成，勿手工编辑生成的 .c 文件。
 */
extern const lv_image_dsc_t ecard_portrait_home;

/**
 * @brief 电子学生证首页的紧凑证件照 (54 × 78, RGB565)。
 */
extern const lv_image_dsc_t ecard_portrait_card;

#ifdef __cplusplus
}
#endif

#endif // ECARD_PHOTO_H
