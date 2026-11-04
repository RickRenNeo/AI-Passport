// main/tianji_audio.h — 天机·六爻: 仪式音效系统
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TIANJI_SFX_START = 0,         // 启动起卦 / 凝神意念
    TIANJI_SFX_TOSS,              // 铜钱掷落碰击声
    TIANJI_SFX_LINE_REVEAL,       // 爻象点亮
    TIANJI_SFX_HEX_COMPLETE,      // 六爻成卦 / 定盘声
    TIANJI_SFX_PAGE_FLIP,         // 翻页查看断语
} tianji_sfx_t;

// 初始化音效系统（在独立低优先级工作任务中流式合成播放，不阻塞按键和UI）
void tianji_audio_init(void);

// 播放指定音效（非阻塞，通过消息队列派发）
void tianji_audio_play(tianji_sfx_t sfx);

// 读取喇叭当前音量（0..100，失败返回 -1）
int tianji_volume_get(void);

// 按 delta 百分比调节喇叭音量，返回调节后的音量（0..100，失败返回 -1）
int tianji_volume_step(int delta);

#ifdef __cplusplus
}
#endif
