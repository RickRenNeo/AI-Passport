// tests/test_tianji_logic.c — 天机·六爻 纯逻辑单元测试
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tianji_logic.h"

// 模拟随机数生成器
static uint32_t s_mock_rand_val = 0;
static uint32_t mock_rand(void) {
    s_mock_rand_val = (s_mock_rand_val * 1103515245 + 12345) & 0x7FFFFFFF;
    return s_mock_rand_val;
}

static void test_line_properties(void) {
    // 7: 少阳 (静阳)
    assert(tianji_line_is_yang(TIANJI_LINE_SHAOYANG) == true);
    assert(tianji_line_is_moving(TIANJI_LINE_SHAOYANG) == false);
    assert(tianji_line_transformed_is_yang(TIANJI_LINE_SHAOYANG) == true);

    // 8: 少阴 (静阴)
    assert(tianji_line_is_yang(TIANJI_LINE_SHAOYIN) == false);
    assert(tianji_line_is_moving(TIANJI_LINE_SHAOYIN) == false);
    assert(tianji_line_transformed_is_yang(TIANJI_LINE_SHAOYIN) == false);

    // 9: 老阳 (动阳，变阴)
    assert(tianji_line_is_yang(TIANJI_LINE_LAOYANG) == true);
    assert(tianji_line_is_moving(TIANJI_LINE_LAOYANG) == true);
    assert(tianji_line_transformed_is_yang(TIANJI_LINE_LAOYANG) == false);

    // 6: 老阴 (动阴，变阳)
    assert(tianji_line_is_yang(TIANJI_LINE_LAOYIN) == false);
    assert(tianji_line_is_moving(TIANJI_LINE_LAOYIN) == true);
    assert(tianji_line_transformed_is_yang(TIANJI_LINE_LAOYIN) == true);
    printf("test_line_properties: PASS\n");
}

static void test_hexagram_lookups(void) {
    // 乾为天: 111111b = 63
    const tianji_hexagram_t *qian = tianji_get_hexagram(63);
    assert(qian != NULL);
    assert(qian->index == 1);
    assert(strcmp(qian->name, "乾为天") == 0);
    assert(strcmp(qian->short_name, "乾") == 0);

    // 坤为地: 000000b = 0
    const tianji_hexagram_t *kun = tianji_get_hexagram(0);
    assert(kun != NULL);
    assert(kun->index == 2);
    assert(strcmp(kun->name, "坤为地") == 0);
    assert(strcmp(kun->short_name, "坤") == 0);

    // 火天大有: 上离(5) 下乾(7) -> (5<<3)|7 = 47
    const tianji_hexagram_t *dayou = tianji_get_hexagram(47);
    assert(dayou != NULL);
    assert(dayou->index == 14);
    assert(strcmp(dayou->name, "火天大有") == 0);

    // 测试 64 卦查表全覆盖，无空指针与非法数据
    for (int i = 0; i < 64; i++) {
        const tianji_hexagram_t *hex = tianji_get_hexagram((uint8_t)i);
        assert(hex != NULL);
        assert(hex->index >= 1 && hex->index <= 64);
        assert(hex->name != NULL && strlen(hex->name) > 0);
        assert(hex->short_name != NULL && strlen(hex->short_name) > 0);
        assert(hex->judgment != NULL && strlen(hex->judgment) > 0);
        assert(hex->career != NULL && strlen(hex->career) > 0);
        assert(hex->wealth != NULL && strlen(hex->wealth) > 0);
        assert(hex->relationship != NULL && strlen(hex->relationship) > 0);
    }

    // 测试 1..64 周易序号索引全覆盖
    for (int i = 1; i <= 64; i++) {
        const tianji_hexagram_t *hex = tianji_get_hexagram_by_index((uint8_t)i);
        assert(hex != NULL);
        assert(hex->index == i);
    }
    printf("test_hexagram_lookups: PASS\n");
}

static void test_session_casting_and_transformation(void) {
    tianji_session_t session;
    tianji_session_init(&session);
    assert(session.current_line == 0);
    assert(session.is_completed == false);

    // 模拟构建 火天大有:
    // 下乾 (初、二、三爻全阳)
    // 上离 (四爻阳、五爻阴、上爻阳)
    // 设定:
    // 初爻: 老阳 (9) -> 动爻，变阴
    // 二爻: 少阳 (7)
    // 三爻: 少阳 (7)
    // 四爻: 少阳 (7)
    // 五爻: 少阴 (8)
    // 上爻: 少阳 (7)
    assert(tianji_session_inject_line(&session, TIANJI_LINE_LAOYANG) == true); // 初爻
    assert(session.current_line == 1);
    assert(session.moving_count == 1);

    assert(tianji_session_inject_line(&session, TIANJI_LINE_SHAOYANG) == true); // 二爻
    assert(tianji_session_inject_line(&session, TIANJI_LINE_SHAOYANG) == true); // 三爻
    assert(tianji_session_inject_line(&session, TIANJI_LINE_SHAOYANG) == true); // 四爻
    assert(tianji_session_inject_line(&session, TIANJI_LINE_SHAOYIN) == true);  // 五爻
    assert(tianji_session_inject_line(&session, TIANJI_LINE_SHAOYANG) == true); // 上爻

    // 第7次注入应返回 false
    assert(tianji_session_inject_line(&session, TIANJI_LINE_SHAOYANG) == false);
    assert(session.is_completed == true);
    assert(session.current_line == 6);

    // 本卦: (离5 << 3) | 乾7 = 47 (火天大有)
    assert(session.primary_bits == 47);
    const tianji_hexagram_t *prim_hex = tianji_get_hexagram(session.primary_bits);
    assert(strcmp(prim_hex->name, "火天大有") == 0);

    // 动爻检验: 唯有初爻(0)动
    assert(session.moving_count == 1);
    assert(session.moving_indices[0] == 0);

    // 变卦: 初爻由阳变阴 -> 下卦变为 巽(风, 110b -> 初爻0, 二爻1, 三爻1) -> 巽=6
    // 上卦离保持不变(5) -> (5<<3)|6 = 46 (火风鼎)
    assert(session.transformed_bits == 46);
    const tianji_hexagram_t *trans_hex = tianji_get_hexagram(session.transformed_bits);
    assert(strcmp(trans_hex->name, "火风鼎") == 0);

    printf("test_session_casting_and_transformation: PASS\n");
}

static void test_random_casting(void) {
    tianji_session_t session;
    tianji_session_init(&session);
    s_mock_rand_val = 1234567;

    for (int i = 0; i < 6; i++) {
        tianji_toss_result_t toss;
        bool ok = tianji_session_cast_step(&session, mock_rand, &toss);
        assert(ok == true);
        assert(toss.sum_score >= 6 && toss.sum_score <= 9);
        assert(toss.coins[0] <= 1 && toss.coins[1] <= 1 && toss.coins[2] <= 1);
    }
    assert(session.is_completed == true);
    assert(session.current_line == 6);
    assert(session.primary_bits < 64);
    assert(session.transformed_bits < 64);
    printf("test_random_casting: PASS\n");
}

int main(void) {
    test_line_properties();
    test_hexagram_lookups();
    test_session_casting_and_transformation();
    test_random_casting();
    printf("All Tianji Liu Yao unit tests passed successfully!\n");
    return 0;
}
