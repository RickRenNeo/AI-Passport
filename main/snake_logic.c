#include "snake_logic.h"
#include <string.h>

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    if (x == 0) x = 0x12345678;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static bool is_cell_occupied(const snake_game_t *game, int8_t x, int8_t y) {
    for (int i = 0; i < game->length; i++) {
        if (game->body[i].x == x && game->body[i].y == y) {
            return true;
        }
    }
    if (game->food.type != SNAKE_FOOD_NONE && game->food.pos.x == x && game->food.pos.y == y) {
        return true;
    }
    if (game->bonus.type != SNAKE_FOOD_NONE && game->bonus.pos.x == x && game->bonus.pos.y == y) {
        return true;
    }
    return false;
}

static bool spawn_food(snake_game_t *game, snake_food_type_t type, int lifetime) {
    int total_cells = SNAKE_GRID_COLS * SNAKE_GRID_ROWS;
    int occupied = game->length;
    if (game->food.type != SNAKE_FOOD_NONE && type != SNAKE_FOOD_NORMAL) occupied++;
    if (game->bonus.type != SNAKE_FOOD_NONE && type != SNAKE_FOOD_BONUS) occupied++;

    int free_cells = total_cells - occupied;
    if (free_cells <= 0) return false;

    uint32_t target_idx = xorshift32(&game->rng_state) % (uint32_t)free_cells;
    uint32_t count = 0;

    for (int8_t y = 0; y < SNAKE_GRID_ROWS; y++) {
        for (int8_t x = 0; x < SNAKE_GRID_COLS; x++) {
            if (!is_cell_occupied(game, x, y)) {
                if (count == target_idx) {
                    if (type == SNAKE_FOOD_NORMAL) {
                        game->food.pos.x = x;
                        game->food.pos.y = y;
                        game->food.type = SNAKE_FOOD_NORMAL;
                        game->food.lifetime_ticks = 0;
                    } else {
                        game->bonus.pos.x = x;
                        game->bonus.pos.y = y;
                        game->bonus.type = SNAKE_FOOD_BONUS;
                        game->bonus.lifetime_ticks = lifetime;
                    }
                    return true;
                }
                count++;
            }
        }
    }
    return false;
}

void snake_init(snake_game_t *game, uint32_t seed, uint32_t high_score) {
    memset(game, 0, sizeof(*game));
    game->rng_state = (seed == 0) ? 0x98765432 : seed;
    game->high_score = high_score;
    game->state = SNAKE_STATE_TITLE;
    game->dir = SNAKE_DIR_RIGHT;
    game->next_dir = SNAKE_DIR_RIGHT;

    // Default initial snake in the middle
    game->length = 4;
    int8_t start_x = SNAKE_GRID_COLS / 2;
    int8_t start_y = SNAKE_GRID_ROWS / 2;
    for (int i = 0; i < game->length; i++) {
        game->body[i].x = start_x - (int8_t)i;
        game->body[i].y = start_y;
    }
}

void snake_start_game(snake_game_t *game) {
    uint32_t seed = game->rng_state;
    uint32_t hi = game->high_score;
    memset(game, 0, sizeof(*game));
    game->rng_state = seed;
    game->high_score = hi;
    game->state = SNAKE_STATE_PLAYING;
    game->dir = SNAKE_DIR_RIGHT;
    game->next_dir = SNAKE_DIR_RIGHT;

    game->length = 4;
    int8_t start_x = 7;
    int8_t start_y = SNAKE_GRID_ROWS / 2;
    for (int i = 0; i < game->length; i++) {
        game->body[i].x = start_x - (int8_t)i;
        game->body[i].y = start_y;
    }

    spawn_food(game, SNAKE_FOOD_NORMAL, 0);
}

snake_event_t snake_input(snake_game_t *game, snake_cmd_t cmd) {
    if (game->state == SNAKE_STATE_TITLE) {
        if (cmd == SNAKE_CMD_OK) {
            snake_start_game(game);
            return SNAKE_EV_START;
        }
        return SNAKE_EV_NONE;
    }

    if (game->state == SNAKE_STATE_GAME_OVER) {
        if (cmd == SNAKE_CMD_OK) {
            snake_start_game(game);
            return SNAKE_EV_START;
        }
        return SNAKE_EV_NONE;
    }

    if (cmd == SNAKE_CMD_OK) {
        if (game->state == SNAKE_STATE_PLAYING) {
            game->state = SNAKE_STATE_PAUSED;
            return SNAKE_EV_PAUSE;
        } else if (game->state == SNAKE_STATE_PAUSED) {
            game->state = SNAKE_STATE_PLAYING;
            return SNAKE_EV_RESUME;
        }
    }

    if (game->state != SNAKE_STATE_PLAYING) {
        return SNAKE_EV_NONE;
    }

    // Relative steering
    if (!game->dir_changed_this_tick) {
        snake_dir_t new_dir;
        if (cmd == SNAKE_CMD_LEFT) {
            new_dir = (snake_dir_t)((game->dir + 3) % 4);
            game->next_dir = new_dir;
            game->dir_changed_this_tick = true;
        } else if (cmd == SNAKE_CMD_RIGHT) {
            new_dir = (snake_dir_t)((game->dir + 1) % 4);
            game->next_dir = new_dir;
            game->dir_changed_this_tick = true;
        }
    }

    return SNAKE_EV_NONE;
}

snake_event_t snake_tick(snake_game_t *game) {
    if (game->state != SNAKE_STATE_PLAYING) {
        return SNAKE_EV_NONE;
    }

    game->tick_count++;
    game->dir = game->next_dir;
    game->dir_changed_this_tick = false;

    // Check bonus food lifetime countdown
    snake_event_t ev = SNAKE_EV_MOVE;
    if (game->bonus.type == SNAKE_FOOD_BONUS) {
        game->bonus.lifetime_ticks--;
        if (game->bonus.lifetime_ticks <= 0) {
            game->bonus.type = SNAKE_FOOD_NONE;
            ev = SNAKE_EV_BONUS_EXPIRE;
        }
    }

    // Determine new head position
    int8_t head_x = game->body[0].x;
    int8_t head_y = game->body[0].y;

    switch (game->dir) {
    case SNAKE_DIR_UP:    head_y--; break;
    case SNAKE_DIR_RIGHT: head_x++; break;
    case SNAKE_DIR_DOWN:  head_y++; break;
    case SNAKE_DIR_LEFT:  head_x--; break;
    }

    // Wall collision
    if (head_x < 0 || head_x >= SNAKE_GRID_COLS || head_y < 0 || head_y >= SNAKE_GRID_ROWS) {
        game->state = SNAKE_STATE_GAME_OVER;
        if (game->score > game->high_score) {
            game->high_score = game->score;
            game->is_new_high_score = true;
        }
        return SNAKE_EV_DIE;
    }

    // Check if eating food (so tail does not shrink)
    bool eating_normal = (game->food.type == SNAKE_FOOD_NORMAL &&
                          head_x == game->food.pos.x && head_y == game->food.pos.y);
    bool eating_bonus  = (game->bonus.type == SNAKE_FOOD_BONUS &&
                          head_x == game->bonus.pos.x && head_y == game->bonus.pos.y);

    // Self collision (if tail moves forward, tail cell is freed unless growing)
    int check_len = (eating_normal || eating_bonus) ? game->length : (game->length - 1);
    for (int i = 0; i < check_len; i++) {
        if (game->body[i].x == head_x && game->body[i].y == head_y) {
            game->state = SNAKE_STATE_GAME_OVER;
            if (game->score > game->high_score) {
                game->high_score = game->score;
                game->is_new_high_score = true;
            }
            return SNAKE_EV_DIE;
        }
    }

    // Move body
    if (eating_normal || eating_bonus) {
        // Grow length by 1
        if (game->length < SNAKE_MAX_LEN) {
            game->length++;
        }
    }

    for (int i = game->length - 1; i > 0; i--) {
        game->body[i] = game->body[i - 1];
    }
    game->body[0].x = head_x;
    game->body[0].y = head_y;

    // Process eaten food
    if (eating_normal) {
        game->score += SNAKE_POINTS_NORMAL;
        if (game->score > game->high_score) {
            game->high_score = game->score;
            game->is_new_high_score = true;
        }
        game->foods_eaten_count++;

        // Spawn new normal food
        spawn_food(game, SNAKE_FOOD_NORMAL, 0);

        // Spawn bonus food every 5 normal foods
        if (game->foods_eaten_count % 5 == 0 && game->bonus.type == SNAKE_FOOD_NONE) {
            spawn_food(game, SNAKE_FOOD_BONUS, SNAKE_BONUS_TICKS);
            return SNAKE_EV_BONUS_SPAWN;
        }
        return SNAKE_EV_EAT_NORMAL;
    }

    if (eating_bonus) {
        game->score += SNAKE_POINTS_BONUS;
        if (game->score > game->high_score) {
            game->high_score = game->score;
            game->is_new_high_score = true;
        }
        game->bonus.type = SNAKE_FOOD_NONE;
        return SNAKE_EV_EAT_BONUS;
    }

    return ev;
}

uint32_t snake_get_tick_ms(const snake_game_t *game) {
    int base_ms = 180;
    int speed_up = (int)(game->score / 30) * 10;
    int interval = base_ms - speed_up;
    if (interval < 80) interval = 80;
    return (uint32_t)interval;
}

bool snake_is_body_cell(const snake_game_t *game, int8_t x, int8_t y) {
    for (int i = 1; i < game->length; i++) {
        if (game->body[i].x == x && game->body[i].y == y) {
            return true;
        }
    }
    return false;
}
