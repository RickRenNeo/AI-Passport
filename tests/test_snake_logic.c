#include "snake_logic.h"
#include <assert.h>
#include <stdio.h>

static void test_snake_init(void) {
    snake_game_t game;
    snake_init(&game, 12345, 100);

    assert(game.state == SNAKE_STATE_TITLE);
    assert(game.length == 4);
    assert(game.high_score == 100);
    assert(game.score == 0);
    assert(game.food.type == SNAKE_FOOD_NONE);
    assert(game.bonus.type == SNAKE_FOOD_NONE);
    printf("test_snake_init: PASS\n");
}

static void test_snake_start_game(void) {
    snake_game_t game;
    snake_init(&game, 12345, 100);

    snake_event_t ev = snake_input(&game, SNAKE_CMD_OK);
    assert(ev == SNAKE_EV_START);
    assert(game.state == SNAKE_STATE_PLAYING);
    assert(game.length == 4);
    assert(game.dir == SNAKE_DIR_RIGHT);
    assert(game.food.type == SNAKE_FOOD_NORMAL);
    assert(game.bonus.type == SNAKE_FOOD_NONE);
    printf("test_snake_start_game: PASS\n");
}

static void test_snake_steering_relative(void) {
    snake_game_t game;
    snake_init(&game, 12345, 0);
    snake_start_game(&game);

    // Initial heading: RIGHT (1)
    // Left turn from RIGHT should be UP (0)
    snake_input(&game, SNAKE_CMD_LEFT);
    snake_tick(&game);
    assert(game.dir == SNAKE_DIR_UP);

    // Left turn from UP should be LEFT (3)
    snake_input(&game, SNAKE_CMD_LEFT);
    snake_tick(&game);
    assert(game.dir == SNAKE_DIR_LEFT);

    // Left turn from LEFT should be DOWN (2)
    snake_input(&game, SNAKE_CMD_LEFT);
    snake_tick(&game);
    assert(game.dir == SNAKE_DIR_DOWN);

    // Right turn from DOWN should be LEFT (3)
    snake_input(&game, SNAKE_CMD_RIGHT);
    snake_tick(&game);
    assert(game.dir == SNAKE_DIR_LEFT);

    // Right turn from LEFT should be UP (0)
    snake_input(&game, SNAKE_CMD_RIGHT);
    snake_tick(&game);
    assert(game.dir == SNAKE_DIR_UP);

    // Right turn from UP should be RIGHT (1)
    snake_input(&game, SNAKE_CMD_RIGHT);
    snake_tick(&game);
    assert(game.dir == SNAKE_DIR_RIGHT);

    printf("test_snake_steering_relative: PASS\n");
}

static void test_snake_prevent_double_turn(void) {
    snake_game_t game;
    snake_init(&game, 12345, 0);
    snake_start_game(&game);

    // Heading RIGHT. If user presses LEFT then LEFT in the same tick:
    // Only the first (UP) should take effect, preventing 180 turn back into body!
    snake_input(&game, SNAKE_CMD_LEFT);
    snake_input(&game, SNAKE_CMD_LEFT);
    snake_tick(&game);
    assert(game.dir == SNAKE_DIR_UP);

    printf("test_snake_prevent_double_turn: PASS\n");
}

static void test_snake_eat_normal_food(void) {
    snake_game_t game;
    snake_init(&game, 12345, 0);
    snake_start_game(&game);

    // Force food to be directly in front of snake head
    int8_t head_x = game.body[0].x;
    int8_t head_y = game.body[0].y;
    game.food.pos.x = head_x + 1;
    game.food.pos.y = head_y;
    game.food.type = SNAKE_FOOD_NORMAL;

    int old_len = game.length;
    snake_event_t ev = snake_tick(&game);

    assert(ev == SNAKE_EV_EAT_NORMAL);
    assert(game.score == SNAKE_POINTS_NORMAL);
    assert(game.length == old_len + 1);
    assert(game.body[0].x == head_x + 1);
    assert(game.body[0].y == head_y);
    assert(game.food.type == SNAKE_FOOD_NORMAL); // New food spawned
    assert(game.foods_eaten_count == 1);

    printf("test_snake_eat_normal_food: PASS\n");
}

static void test_snake_bonus_food_lifecycle(void) {
    snake_game_t game;
    snake_init(&game, 12345, 0);
    snake_start_game(&game);

    // Eat 4 foods
    game.foods_eaten_count = 4;
    // Set 5th food in front
    game.food.pos.x = game.body[0].x + 1;
    game.food.pos.y = game.body[0].y;
    game.food.type = SNAKE_FOOD_NORMAL;

    snake_event_t ev = snake_tick(&game);
    assert(ev == SNAKE_EV_BONUS_SPAWN);
    assert(game.bonus.type == SNAKE_FOOD_BONUS);
    assert(game.bonus.lifetime_ticks == SNAKE_BONUS_TICKS);

    // Test eating bonus food
    game.bonus.pos.x = game.body[0].x + 1;
    game.bonus.pos.y = game.body[0].y;
    // Move away normal food to avoid overlap
    game.food.pos.x = 0;
    game.food.pos.y = 0;

    ev = snake_tick(&game);
    assert(ev == SNAKE_EV_EAT_BONUS);
    assert(game.score == SNAKE_POINTS_NORMAL + SNAKE_POINTS_BONUS);
    assert(game.bonus.type == SNAKE_FOOD_NONE);

    // Test bonus food expiration if not eaten
    game.foods_eaten_count = 4;
    game.food.pos.x = game.body[0].x + 1;
    game.food.pos.y = game.body[0].y;
    snake_tick(&game); // spawns bonus
    assert(game.bonus.type == SNAKE_FOOD_BONUS);

    // Move bonus food far away
    game.bonus.pos.x = 0;
    game.bonus.pos.y = 0;
    game.bonus.lifetime_ticks = 2;

    snake_tick(&game); // lifetime becomes 1
    assert(game.bonus.type == SNAKE_FOOD_BONUS);

    ev = snake_tick(&game); // lifetime becomes 0, expires
    assert(ev == SNAKE_EV_BONUS_EXPIRE);
    assert(game.bonus.type == SNAKE_FOOD_NONE);

    printf("test_snake_bonus_food_lifecycle: PASS\n");
}

static void test_snake_wall_collision(void) {
    snake_game_t game;
    snake_init(&game, 12345, 0);
    snake_start_game(&game);

    // Move snake head near right wall (col 19)
    game.body[0].x = SNAKE_GRID_COLS - 1;
    game.body[0].y = 5;
    game.dir = SNAKE_DIR_RIGHT;
    game.next_dir = SNAKE_DIR_RIGHT;

    snake_event_t ev = snake_tick(&game);
    assert(ev == SNAKE_EV_DIE);
    assert(game.state == SNAKE_STATE_GAME_OVER);

    printf("test_snake_wall_collision: PASS\n");
}

static void test_snake_self_collision(void) {
    snake_game_t game;
    snake_init(&game, 12345, 0);
    snake_start_game(&game);

    // Snake of length 5:
    // body[0] = (5, 5) (head)
    // body[1] = (5, 6)
    // body[2] = (4, 6)
    // body[3] = (4, 5) (intermediate body segment)
    // body[4] = (4, 4) (tail)
    // Moving LEFT from (5, 5) enters (4, 5), which hits body[3]!
    game.length = 5;
    game.body[0].x = 5; game.body[0].y = 5;
    game.body[1].x = 5; game.body[1].y = 6;
    game.body[2].x = 4; game.body[2].y = 6;
    game.body[3].x = 4; game.body[3].y = 5;
    game.body[4].x = 4; game.body[4].y = 4;
    game.dir = SNAKE_DIR_LEFT;
    game.next_dir = SNAKE_DIR_LEFT;

    snake_event_t ev = snake_tick(&game);
    assert(ev == SNAKE_EV_DIE);
    assert(game.state == SNAKE_STATE_GAME_OVER);

    printf("test_snake_self_collision: PASS\n");
}

static void test_snake_pause_and_resume(void) {
    snake_game_t game;
    snake_init(&game, 12345, 0);
    snake_start_game(&game);

    snake_event_t ev = snake_input(&game, SNAKE_CMD_OK);
    assert(ev == SNAKE_EV_PAUSE);
    assert(game.state == SNAKE_STATE_PAUSED);

    ev = snake_tick(&game);
    assert(ev == SNAKE_EV_NONE);

    ev = snake_input(&game, SNAKE_CMD_OK);
    assert(ev == SNAKE_EV_RESUME);
    assert(game.state == SNAKE_STATE_PLAYING);

    printf("test_snake_pause_and_resume: PASS\n");
}

static void test_snake_speed_scaling(void) {
    snake_game_t game;
    snake_init(&game, 12345, 0);
    snake_start_game(&game);

    assert(snake_get_tick_ms(&game) == 180);

    game.score = 60;
    assert(snake_get_tick_ms(&game) == 160);

    game.score = 150;
    assert(snake_get_tick_ms(&game) == 130);

    game.score = 400;
    assert(snake_get_tick_ms(&game) == 80); // Capped at minimum 80ms

    printf("test_snake_speed_scaling: PASS\n");
}

int main(void) {
    test_snake_init();
    test_snake_start_game();
    test_snake_steering_relative();
    test_snake_prevent_double_turn();
    test_snake_eat_normal_food();
    test_snake_bonus_food_lifecycle();
    test_snake_wall_collision();
    test_snake_self_collision();
    test_snake_pause_and_resume();
    test_snake_speed_scaling();
    printf("All Snake logic unit tests passed successfully!\n");
    return 0;
}
