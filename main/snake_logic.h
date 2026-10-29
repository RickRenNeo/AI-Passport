#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SNAKE_GRID_COLS 20
#define SNAKE_GRID_ROWS 24
#define SNAKE_MAX_LEN   (SNAKE_GRID_COLS * SNAKE_GRID_ROWS)

#define SNAKE_POINTS_NORMAL 10
#define SNAKE_POINTS_BONUS  50
#define SNAKE_BONUS_TICKS   45

typedef enum {
    SNAKE_DIR_UP = 0,
    SNAKE_DIR_RIGHT = 1,
    SNAKE_DIR_DOWN = 2,
    SNAKE_DIR_LEFT = 3,
} snake_dir_t;

typedef enum {
    SNAKE_STATE_TITLE = 0,
    SNAKE_STATE_PLAYING,
    SNAKE_STATE_PAUSED,
    SNAKE_STATE_GAME_OVER,
} snake_state_t;

typedef enum {
    SNAKE_FOOD_NONE = 0,
    SNAKE_FOOD_NORMAL = 1,
    SNAKE_FOOD_BONUS = 2,
} snake_food_type_t;

typedef struct {
    int8_t x;
    int8_t y;
} snake_point_t;

typedef struct {
    snake_point_t pos;
    snake_food_type_t type;
    int lifetime_ticks;
} snake_food_t;

typedef struct {
    snake_state_t state;
    snake_dir_t dir;
    snake_dir_t next_dir;
    bool dir_changed_this_tick;

    snake_point_t body[SNAKE_MAX_LEN];
    int length;

    snake_food_t food;
    snake_food_t bonus;
    int foods_eaten_count;

    uint32_t score;
    uint32_t high_score;
    bool is_new_high_score;

    uint32_t tick_count;
    uint32_t rng_state;
} snake_game_t;

typedef enum {
    SNAKE_EV_NONE = 0,
    SNAKE_EV_MOVE,
    SNAKE_EV_EAT_NORMAL,
    SNAKE_EV_EAT_BONUS,
    SNAKE_EV_BONUS_SPAWN,
    SNAKE_EV_BONUS_EXPIRE,
    SNAKE_EV_DIE,
    SNAKE_EV_START,
    SNAKE_EV_PAUSE,
    SNAKE_EV_RESUME,
} snake_event_t;

typedef enum {
    SNAKE_CMD_LEFT = 0,   // Relative left turn (UP button)
    SNAKE_CMD_RIGHT,      // Relative right turn (DOWN button)
    SNAKE_CMD_OK,         // Confirm / Pause / Start / Restart (OK button)
} snake_cmd_t;

/* Initialize or reset game instance */
void snake_init(snake_game_t *game, uint32_t seed, uint32_t high_score);

/* Reset round to start playing */
void snake_start_game(snake_game_t *game);

/* Process button input command */
snake_event_t snake_input(snake_game_t *game, snake_cmd_t cmd);

/* Advance game by one tick */
snake_event_t snake_tick(snake_game_t *game);

/* Calculate tick interval in ms based on current score */
uint32_t snake_get_tick_ms(const snake_game_t *game);

/* Query whether a grid cell has snake body (excluding head) */
bool snake_is_body_cell(const snake_game_t *game, int8_t x, int8_t y);
