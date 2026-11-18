#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#include "ecard_cards.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int count_pak_files(const char *dir_path, bool present[ECARD_GRADE_COUNT + 1]) {
    for (int i = 0; i <= ECARD_GRADE_COUNT; ++i) {
        present[i] = false;
    }
    DIR *d = opendir(dir_path);
    assert(d != NULL);
    int total = 0;
    struct dirent *ent = NULL;
    while ((ent = readdir(d)) != NULL) {
        unsigned g = 0;
        char tail = '\0';
        if (sscanf(ent->d_name, "cur_g%u.pak%c", &g, &tail) == 1 &&
            g >= 1 && g <= ECARD_GRADE_COUNT) {
            present[g] = true;
            ++total;
        }
    }
    closedir(d);
    return total;
}

static void remove_dir_files(const char *dir_path) {
    DIR *d = opendir(dir_path);
    if (!d) return;
    struct dirent *ent = NULL;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        char path[1024];
        const int n = snprintf(path, sizeof(path), "%s/%s", dir_path, ent->d_name);
        if (n <= 0 || (size_t)n >= sizeof(path)) {
            continue;
        }
        (void)unlink(path);
    }
    closedir(d);
    (void)rmdir(dir_path);
}

static void test_parse_grade_and_default_windows(void) {
    assert(ecard_cards_parse_grade_from_class("一年级(1)班") == 1);
    assert(ecard_cards_parse_grade_from_class("二(3)班") == 2);
    assert(ecard_cards_parse_grade_from_class("三年级(2)班") == 3);
    assert(ecard_cards_parse_grade_from_class("402班") == 4);
    assert(ecard_cards_parse_grade_from_class("五年级一班") == 5);
    assert(ecard_cards_parse_grade_from_class("G6") == 6);
    assert(ecard_cards_parse_grade_from_class("") == 0);
    assert(ecard_cards_parse_grade_from_class(NULL) == 0);

    uint8_t grades[ECARD_MAX_ACTIVE_GRADES] = {0};
    uint8_t count = 0;

    ecard_cards_default_grades_for(1, grades, &count);
    assert(count == 3 && grades[0] == 1 && grades[1] == 2 && grades[2] == 3);

    ecard_cards_default_grades_for(2, grades, &count);
    assert(count == 3 && grades[0] == 1 && grades[1] == 2 && grades[2] == 3);

    ecard_cards_default_grades_for(3, grades, &count);
    assert(count == 3 && grades[0] == 2 && grades[1] == 3 && grades[2] == 4);

    ecard_cards_default_grades_for(4, grades, &count);
    assert(count == 3 && grades[0] == 3 && grades[1] == 4 && grades[2] == 5);

    ecard_cards_default_grades_for(5, grades, &count);
    assert(count == 3 && grades[0] == 4 && grades[1] == 5 && grades[2] == 6);

    ecard_cards_default_grades_for(6, grades, &count);
    assert(count == 3 && grades[0] == 4 && grades[1] == 5 && grades[2] == 6);
}

static void test_full_curriculum_coverage_unmounted(void) {
    ecard_cards_reset_curriculum();

    uint16_t total_poem_cards = 0;
    uint16_t total_prose_cards = 0;
    uint16_t total_word_cards = 0;

    for (uint8_t g = 1; g <= ECARD_GRADE_COUNT; ++g) {
        const uint8_t poem_decks = ecard_cards_deck_count(ECARD_CARD_POEM, g);
        const uint8_t word_decks = ecard_cards_deck_count(ECARD_CARD_WORD, g);
        /* 每个年级不能只有 10 题：古诗古文拆成 2~3 组，英语按上下册拆成 12 个单元 */
        assert(poem_decks >= 2 && poem_decks <= 4);
        assert(word_decks == 12);

        uint16_t grade_poems = 0;
        for (uint8_t d = 0; d < poem_decks; ++d) {
            ecard_deck_info_t info;
            assert(ecard_cards_deck_info(ECARD_CARD_POEM, g, d, &info));
            assert(info.title[0] != '\0');
            assert(info.subtitle[0] != '\0');
            assert(info.card_count >= 5 && info.card_count <= ECARD_MAX_CARDS_PER_DECK);

            uint8_t loaded = 0;
            const ecard_card_t *cards = ecard_cards_load_deck(ECARD_CARD_POEM, g, d, &loaded);
            assert(cards != NULL);
            assert(loaded == info.card_count);
            if (strstr(info.title, "古文") != NULL) {
                total_prose_cards += loaded;
            }
            for (uint8_t i = 0; i < loaded; ++i) {
                assert(cards[i].front && cards[i].front[0] != '\0');
                assert(cards[i].back && cards[i].back[0] != '\0');
                assert(cards[i].meta && cards[i].meta[0] != '\0');
                assert(cards[i].explain && cards[i].explain[0] != '\0');
                assert(cards[i].example == NULL);
            }
            grade_poems += loaded;
        }
        assert(grade_poems >= 14);
        assert(ecard_cards_grade_card_total(ECARD_CARD_POEM, g) == grade_poems);
        total_poem_cards += grade_poems;

        uint16_t grade_words = 0;
        for (uint8_t d = 0; d < word_decks; ++d) {
            ecard_deck_info_t info;
            assert(ecard_cards_deck_info(ECARD_CARD_WORD, g, d, &info));
            assert(strstr(info.title, "Unit") != NULL);
            assert(info.card_count >= 8 && info.card_count <= ECARD_MAX_CARDS_PER_DECK);

            uint8_t loaded = 0;
            const ecard_card_t *cards = ecard_cards_load_deck(ECARD_CARD_WORD, g, d, &loaded);
            assert(cards != NULL);
            assert(loaded == info.card_count);
            for (uint8_t i = 0; i < loaded; ++i) {
                assert(cards[i].front && cards[i].front[0] != '\0');
                assert(cards[i].back && cards[i].back[0] != '\0');
                assert(cards[i].example && cards[i].example[0] != '\0');
                assert(cards[i].example_cn && cards[i].example_cn[0] != '\0');
            }
            grade_words += loaded;
        }
        assert(grade_words >= 100);
        assert(ecard_cards_grade_card_total(ECARD_CARD_WORD, g) == grade_words);
        total_word_cards += grade_words;
    }

    /* 覆盖小学 1~6 年级全部 75 首必背古诗 + 课标小古文（>=95 张卡）及 >=700 个单元核心词 */
    assert(total_poem_cards >= 95);
    assert(total_prose_cards >= 12);
    assert(total_word_cards >= 700);
    assert(ecard_cards_word_total() == total_word_cards);
}

static void test_spiffs_three_grade_retention_and_pruning(void) {
    ecard_cards_reset_curriculum();

    char tmp_template[] = "/tmp/ecard_pak_test_XXXXXX";
    char *dir = mkdtemp(tmp_template);
    assert(dir != NULL);

    /* 默认 3 年级 -> 仅生成 cur_g2.pak, cur_g3.pak, cur_g4.pak 三个文件 */
    assert(ecard_cards_mount_dir(dir));
    bool present[ECARD_GRADE_COUNT + 1];
    assert(count_pak_files(dir, present) == 3);
    assert(!present[1] && present[2] && present[3] && present[4] && !present[5] && !present[6]);

    /* 未保留年级在设备模式下拒绝加载；保留年级正常从 .pak 按单元加载 */
    uint8_t cnt = 0;
    assert(ecard_cards_load_deck(ECARD_CARD_WORD, 1, 0, &cnt) == NULL);
    assert(cnt == 0);
    const ecard_card_t *g3_u5 = ecard_cards_load_deck(ECARD_CARD_WORD, 3, 4, &cnt);
    assert(g3_u5 != NULL && cnt >= 8);
    char saved_front[32];
    snprintf(saved_front, sizeof(saved_front), "%s", g3_u5[0].front);

    /* 在加载某单元后查询其他分组 deck_info，不得污染当前已加载的闪卡缓冲区 */
    ecard_deck_info_t other_info;
    assert(ecard_cards_deck_info(ECARD_CARD_POEM, 2, 0, &other_info));
    assert(strcmp(g3_u5[0].front, saved_front) == 0);

    /* 家长端切换为当前 6 年级（自定义保留 [5, 6] 两个年级）：旧 g2/g3/g4 必须被物理删除 */
    const uint8_t custom_grades[2] = {6, 5};
    assert(ecard_cards_set_curriculum(6, custom_grades, 2));
    assert(ecard_cards_current_grade() == 6);
    assert(count_pak_files(dir, present) == 2);
    assert(!present[1] && !present[2] && !present[3] && !present[4] && present[5] && present[6]);
    assert(ecard_cards_load_deck(ECARD_CARD_POEM, 3, 0, &cnt) == NULL);
    assert(ecard_cards_load_deck(ECARD_CARD_POEM, 6, 0, &cnt) != NULL && cnt >= 5);

    /* 非法参数（超过 3 个年级、越界年级 0/7）必须拒绝且保持原状态不变 */
    const uint8_t four_grades[4] = {1, 2, 3, 4};
    assert(!ecard_cards_set_curriculum(2, four_grades, 4));
    const uint8_t bad_grade[1] = {7};
    assert(!ecard_cards_set_curriculum(6, bad_grade, 1));
    assert(!ecard_cards_set_curriculum(0, NULL, 0));
    assert(ecard_cards_current_grade() == 6);
    assert(count_pak_files(dir, present) == 2);

    ecard_cards_reset_curriculum();
    remove_dir_files(dir);
}

int main(void) {
    printf("Running ecard_curriculum tests...\n");
    test_parse_grade_and_default_windows();
    test_full_curriculum_coverage_unmounted();
    test_spiffs_three_grade_retention_and_pruning();
    printf("ecard_curriculum tests passed!\n");
    return 0;
}
