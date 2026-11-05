#include "ecard_app.h"
#include "ecard_profile.h"
#include "ecard_ui.h"
#include "ecard_voice.h"
#include <assert.h>
#include <stdio.h>

static void test_ecard_app_recording_lifecycle(void) {
    ecard_app_init();
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);

    // 1. 长按 OK 开始录音
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_app_get_state() == ECARD_APP_STATE_RECORDING);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_RECORDING);

    // 模拟录入几帧音频
    ecard_app_simulate_audio_tick();
    assert(ecard_app_get_state() == ECARD_APP_STATE_RECORDING);

    // 2. 短按 OK 提前结束录音并上传
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);
}

static void test_ecard_app_message_play_and_mutual_exclusion(void) {
    ecard_app_init();
    assert(!ecard_profile_has_unread());

    // 1. 短按 UP 触发手动同步
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_CLICK);
    assert(ecard_profile_has_unread()); // 获取到了未读留言

    // 2. 短按 OK 触发收听
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_app_get_state() == ECARD_APP_STATE_PLAYING);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_PLAYING);

    // 3. 推进时钟至播完 (12 秒)
    for (int s = 0; s < 12; ++s) {
        ecard_app_tick(10, 0, 1000);
    }

    // 播放完毕，自动标记已读，复原为姓名班级，退出模态
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);
    assert(!ecard_profile_has_unread());
}

static void test_ecard_app_sos_alert(void) {
    ecard_app_init();
    // 长按 DOWN 触发紧急报警
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_LONG);
    // 发送完毕恢复空闲
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
}

static void test_ecard_app_recess_polling(void) {
    ecard_app_init();
    assert(!ecard_profile_has_unread());

    // 09:30 为上课时段，不触发同步
    ecard_app_tick(9, 30, 1000);
    assert(!ecard_profile_has_unread());

    // 09:40 跨入上午大课间，自动触发一次同步
    ecard_app_tick(9, 40, 1000);
    assert(ecard_profile_has_unread());
}

int main(void) {
    printf("Running ecard_app tests...\n");
    test_ecard_app_recording_lifecycle();
    test_ecard_app_message_play_and_mutual_exclusion();
    test_ecard_app_sos_alert();
    test_ecard_app_recess_polling();
    printf("ecard_app tests passed!\n");
    return 0;
}
