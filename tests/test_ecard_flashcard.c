#include "ecard_cards.h"
#include "ecard_flashcard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void load_deck(bool poem, uint8_t grade) {
    uint8_t count = 0;
    const ecard_card_t *cards = poem ? ecard_cards_poems(grade, &count)
                                     : ecard_cards_words(grade, &count);
    assert(cards != NULL);
    assert(count > 0);
    ecard_flashcard_load(cards, count, poem ? 0 : ecard_cards_word_offset(grade));
}

static void test_every_grade_has_cards(void) {
    for (uint8_t grade = 1; grade <= ECARD_GRADE_COUNT; ++grade) {
        uint8_t poem_count = 0;
        uint8_t word_count = 0;
        assert(ecard_cards_poems(grade, &poem_count) != NULL);
        assert(ecard_cards_words(grade, &word_count) != NULL);
        assert(poem_count > 0);
        assert(word_count > 0);
        assert(poem_count <= ECARD_FLASHCARD_MAX_CARDS);
        assert(word_count <= ECARD_FLASHCARD_MAX_CARDS);

        const ecard_card_t *poems = ecard_cards_poems(grade, NULL);
        for (uint8_t i = 0; i < poem_count; ++i) {
            assert(poems[i].front && poems[i].front[0] != '\0');
            assert(poems[i].back && poems[i].back[0] != '\0');
            assert(poems[i].meta && poems[i].meta[0] != '\0');
            assert(poems[i].explain && poems[i].explain[0] != '\0');
            assert(poems[i].example == NULL);
        }

        const ecard_card_t *words = ecard_cards_words(grade, NULL);
        for (uint8_t i = 0; i < word_count; ++i) {
            assert(words[i].front && words[i].front[0] != '\0');
            assert(words[i].back && words[i].back[0] != '\0');
            assert(words[i].example && words[i].example[0] != '\0');
            assert(words[i].example_cn && words[i].example_cn[0] != '\0');
        }
    }
}

static void test_word_audio_index_is_contiguous(void) {
    ecard_cards_reset_curriculum();
    uint16_t expected = 0;
    for (uint8_t grade = 1; grade <= ECARD_GRADE_COUNT; ++grade) {
        assert(ecard_cards_word_offset(grade) == expected);
        const uint8_t decks = ecard_cards_deck_count(ECARD_CARD_WORD, grade);
        assert(decks == 12);
        for (uint8_t d = 0; d < decks; ++d) {
            uint8_t count = 0;
            assert(ecard_cards_word_deck_offset(grade, d) == expected);
            assert(ecard_cards_load_deck(ECARD_CARD_WORD, grade, d, &count) != NULL);
            expected += count;
        }
    }
    assert(ecard_cards_word_total() == expected);
    assert(expected <= 1000);
}

static void test_load_and_mark(void) {
    load_deck(true, 3);
    const uint8_t total = ecard_flashcard_total();
    assert(total > 0);
    assert(ecard_flashcard_remaining() == total);
    assert(ecard_flashcard_known() == 0);
    assert(!ecard_flashcard_is_flipped());
    assert(ecard_flashcard_current() != NULL);
    assert(ecard_flashcard_index() == 0);

    // 古诗无发音，audio_index 恒为 0
    assert(ecard_flashcard_audio_index() == 0);

    // 未翻面不能记录
    assert(!ecard_flashcard_mark(true));

    ecard_flashcard_flip();
    assert(ecard_flashcard_mark(true));
    assert(ecard_flashcard_remaining() == total - 1);
    assert(ecard_flashcard_index() == 1);
}

static void test_word_deck_audio_index(void) {
    load_deck(false, 4);
    const uint16_t base = ecard_cards_word_offset(4);
    assert(base > 0);
    assert(ecard_flashcard_audio_index() == base);
    assert(ecard_flashcard_total() <= ECARD_FLASHCARD_MAX_CARDS);

    ecard_flashcard_flip();
    ecard_flashcard_mark(true);
    assert(ecard_flashcard_audio_index() == base + 1);
}

static void test_unknown_requeues_to_tail(void) {
    load_deck(true, 2);
    const uint8_t total = ecard_flashcard_total();
    const ecard_card_t *first = ecard_flashcard_current();
    char front[48];
    snprintf(front, sizeof(front), "%s", first->front);

    ecard_flashcard_flip();
    assert(ecard_flashcard_mark(false));
    assert(ecard_flashcard_remaining() == total);
    assert(ecard_flashcard_known() == 0);

    bool found_again = false;
    for (uint8_t i = 0; i < total; ++i) {
        if (strcmp(ecard_flashcard_current()->front, front) == 0) {
            found_again = true;
            break;
        }
        ecard_flashcard_flip();
        assert(ecard_flashcard_mark(true));
    }
    assert(found_again);
}

static void test_full_round_to_finish(void) {
    load_deck(false, 6);
    const uint8_t total = ecard_flashcard_total();
    int guard = 0;
    while (!ecard_flashcard_is_finished() && guard++ < 200) {
        ecard_flashcard_flip();
        ecard_flashcard_mark(true);
    }
    assert(ecard_flashcard_is_finished());
    assert(ecard_flashcard_known() == total);
    assert(ecard_flashcard_remaining() == 0);
    assert(ecard_flashcard_current() == NULL);
    ecard_flashcard_flip();
    assert(!ecard_flashcard_is_flipped());
    assert(!ecard_flashcard_mark(true));
}

static void test_empty_load(void) {
    ecard_flashcard_load(NULL, 0, 0);
    assert(ecard_flashcard_total() == 0);
    assert(ecard_flashcard_current() == NULL);
    assert(!ecard_flashcard_is_finished());
}

int main(void) {
    printf("Running ecard_flashcard tests...\n");
    test_every_grade_has_cards();
    test_word_audio_index_is_contiguous();
    test_load_and_mark();
    test_word_deck_audio_index();
    test_unknown_requeues_to_tail();
    test_full_round_to_finish();
    test_empty_load();
    printf("ecard_flashcard tests passed!\n");
    return 0;
}
