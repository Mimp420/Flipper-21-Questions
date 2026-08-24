#include <furi.h>
#include <furi_hal_random.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include <stdio.h>

#define SNAKE_MAX 48
#define MEMORY_MAX 12

typedef enum { ScreenMenu, ScreenGame } Screen;
typedef enum { GameReaction, GameSnake, GameDice, GameMemory, GameTreasure } GameType;

typedef struct { int8_t x; int8_t y; } Point;

typedef struct {
    Gui* gui;
    ViewPort* viewport;
    FuriMessageQueue* queue;
    FuriMutex* mutex;
    bool running;
    Screen screen;
    uint8_t menu;
    GameType game;
    char message[32];
    uint32_t score;

    uint8_t reaction_state;
    uint32_t reaction_ready_at;

    Point snake[SNAKE_MAX];
    uint8_t snake_length;
    int8_t snake_dx;
    int8_t snake_dy;
    Point food;
    bool snake_over;

    uint8_t player_die;
    uint8_t flipper_die;

    uint8_t sequence[MEMORY_MAX];
    uint8_t sequence_length;
    uint8_t sequence_pos;
    uint8_t memory_selected;
    bool memory_over;

    Point player;
    Point treasure;
    uint8_t moves;
    bool treasure_won;
} Arcade;

static const char* game_names[] = {
    "Reaction Test", "Pocket Snake", "Dice Duel", "Memory Lights", "Treasure Hunt"};

static uint32_t random_range(uint32_t max) {
    return max ? furi_hal_random_get() % max : 0;
}

static void center(Canvas* canvas, uint8_t y, const char* text) {
    canvas_draw_str_aligned(canvas, 64, y, AlignCenter, AlignCenter, text);
}

static void set_message(Arcade* app, const char* text) {
    snprintf(app->message, sizeof(app->message), "%s", text);
}

static void place_food(Arcade* app) {
    bool occupied;
    do {
        occupied = false;
        app->food.x = random_range(16);
        app->food.y = random_range(8);
        for(uint8_t i = 0; i < app->snake_length; i++) {
            if(app->snake[i].x == app->food.x && app->snake[i].y == app->food.y) occupied = true;
        }
    } while(occupied);
}

static void start_selected_game(Arcade* app) {
    app->game = (GameType)app->menu;
    app->screen = ScreenGame;
    app->score = 0;
    set_message(app, "");

    if(app->game == GameReaction) {
        app->reaction_state = 0;
        set_message(app, "Press OK to arm");
    } else if(app->game == GameSnake) {
        app->snake_length = 3;
        app->snake[0] = (Point){8, 4};
        app->snake[1] = (Point){7, 4};
        app->snake[2] = (Point){6, 4};
        app->snake_dx = 1;
        app->snake_dy = 0;
        app->snake_over = false;
        place_food(app);
    } else if(app->game == GameDice) {
        app->player_die = 0;
        app->flipper_die = 0;
        set_message(app, "OK to roll");
    } else if(app->game == GameMemory) {
        app->sequence_length = 1;
        app->sequence_pos = 0;
        app->memory_selected = 0;
        app->memory_over = false;
        for(uint8_t i = 0; i < MEMORY_MAX; i++) app->sequence[i] = random_range(4);
        set_message(app, "Match the pattern");
    } else {
        app->player = (Point){0, 0};
        do {
            app->treasure.x = random_range(8);
            app->treasure.y = random_range(5);
        } while(app->treasure.x == 0 && app->treasure.y == 0);
        app->moves = 0;
        app->treasure_won = false;
    }
}

static void snake_step(Arcade* app) {
    if(app->snake_over) return;
    Point next = {app->snake[0].x + app->snake_dx, app->snake[0].y + app->snake_dy};
    if(next.x < 0 || next.x >= 16 || next.y < 0 || next.y >= 8) {
        app->snake_over = true;
        return;
    }
    for(uint8_t i = 0; i < app->snake_length; i++) {
        if(app->snake[i].x == next.x && app->snake[i].y == next.y) {
            app->snake_over = true;
            return;
        }
    }
    bool ate = next.x == app->food.x && next.y == app->food.y;
    uint8_t limit = app->snake_length;
    if(ate && app->snake_length < SNAKE_MAX) app->snake_length++;
    for(uint8_t i = app->snake_length - 1; i > 0; i--) {
        app->snake[i] = app->snake[i - 1 < limit ? i - 1 : limit - 1];
    }
    app->snake[0] = next;
    if(ate) {
        app->score++;
        place_food(app);
    }
}

static void draw_menu(Canvas* canvas, Arcade* app) {
    canvas_set_font(canvas, FontPrimary);
    center(canvas, 8, "MINI ARCADE");
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t row = 0; row < 3; row++) {
        int8_t index = (int8_t)app->menu + row - 1;
        if(index < 0) index += 5;
        if(index >= 5) index -= 5;
        if(row == 1) {
            canvas_draw_rframe(canvas, 9, 24, 110, 15, 3);
            canvas_set_font(canvas, FontPrimary);
        }
        center(canvas, 17 + row * 14, game_names[index]);
        canvas_set_font(canvas, FontSecondary);
    }
    center(canvas, 60, "UP/DOWN Choose   OK Play");
}

static void draw_reaction(Canvas* canvas, Arcade* app) {
    canvas_set_font(canvas, FontPrimary);
    center(canvas, 10, "REACTION TEST");
    if(app->reaction_state == 1) {
        canvas_set_font(canvas, FontBigNumbers);
        center(canvas, 36, "WAIT");
    } else if(app->reaction_state == 2) {
        canvas_set_font(canvas, FontBigNumbers);
        center(canvas, 36, "GO!");
    } else {
        canvas_set_font(canvas, FontSecondary);
        center(canvas, 35, app->message);
    }
    canvas_set_font(canvas, FontSecondary);
    center(canvas, 59, "OK = Action   BACK = Menu");
}

static void draw_snake(Canvas* canvas, Arcade* app) {
    char status[24];
    snprintf(status, sizeof(status), "SNAKE  Score:%lu", (unsigned long)app->score);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 8, status);
    canvas_draw_frame(canvas, 15, 13, 98, 50);
    for(uint8_t i = 0; i < app->snake_length; i++)
        canvas_draw_box(canvas, 17 + app->snake[i].x * 6, 15 + app->snake[i].y * 6, 5, 5);
    canvas_draw_circle(canvas, 19 + app->food.x * 6, 17 + app->food.y * 6, 2);
    if(app->snake_over) {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 30, 27, 68, 20);
        canvas_set_color(canvas, ColorBlack);
        canvas_set_font(canvas, FontPrimary);
        center(canvas, 38, "GAME OVER");
    }
}

static void draw_dice(Canvas* canvas, Arcade* app) {
    char left[8], right[8];
    snprintf(left, sizeof(left), "%u", app->player_die);
    snprintf(right, sizeof(right), "%u", app->flipper_die);
    canvas_set_font(canvas, FontPrimary);
    center(canvas, 9, "DICE DUEL");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 13, 23, "YOU");
    canvas_draw_str(canvas, 83, 23, "FLIPPER");
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str(canvas, 22, 45, left);
    canvas_draw_str(canvas, 95, 45, right);
    canvas_set_font(canvas, FontSecondary);
    center(canvas, 57, app->message);
}

static const char* arrow_name(uint8_t value) {
    static const char* names[] = {"UP", "RIGHT", "DOWN", "LEFT"};
    return names[value & 3];
}

static void draw_memory(Canvas* canvas, Arcade* app) {
    char status[24];
    canvas_set_font(canvas, FontPrimary);
    center(canvas, 9, "MEMORY LIGHTS");
    canvas_set_font(canvas, FontSecondary);
    snprintf(status, sizeof(status), "Round %u  Step %u/%u", app->sequence_length,
        app->sequence_pos + 1, app->sequence_length);
    center(canvas, 22, status);
    canvas_set_font(canvas, FontPrimary);
    center(canvas, 39, app->memory_over ? "GAME OVER" : arrow_name(app->memory_selected));
    canvas_set_font(canvas, FontSecondary);
    center(canvas, 57, app->memory_over ? "OK = Restart" : "Arrows choose, OK enter");
}

static void draw_treasure(Canvas* canvas, Arcade* app) {
    char status[24];
    canvas_set_font(canvas, FontSecondary);
    snprintf(status, sizeof(status), "TREASURE  Moves:%u", app->moves);
    canvas_draw_str(canvas, 2, 8, status);
    for(uint8_t x = 0; x <= 8; x++) canvas_draw_line(canvas, 16 + x * 12, 12, 16 + x * 12, 62);
    for(uint8_t y = 0; y <= 5; y++) canvas_draw_line(canvas, 16, 12 + y * 10, 112, 12 + y * 10);
    canvas_draw_box(canvas, 19 + app->player.x * 12, 15 + app->player.y * 10, 6, 5);
    if(app->treasure_won) {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 25, 27, 78, 20);
        canvas_set_color(canvas, ColorBlack);
        canvas_set_font(canvas, FontPrimary);
        center(canvas, 38, "TREASURE FOUND!");
    }
}

static void draw_callback(Canvas* canvas, void* context) {
    Arcade* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);
    if(app->screen == ScreenMenu) draw_menu(canvas, app);
    else if(app->game == GameReaction) draw_reaction(canvas, app);
    else if(app->game == GameSnake) draw_snake(canvas, app);
    else if(app->game == GameDice) draw_dice(canvas, app);
    else if(app->game == GameMemory) draw_memory(canvas, app);
    else draw_treasure(canvas, app);
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    Arcade* app = context;
    furi_message_queue_put(app->queue, event, 0);
}

static void handle_reaction(Arcade* app, InputKey key) {
    uint32_t now = furi_get_tick();
    if(app->reaction_state == 0 && key == InputKeyOk) {
        app->reaction_state = 1;
        app->reaction_ready_at = now + furi_ms_to_ticks(1500 + random_range(3000));
    } else if(app->reaction_state == 1 && key == InputKeyOk) {
        app->reaction_state = 0;
        set_message(app, "Too soon! Try again");
    } else if(app->reaction_state == 2 && key == InputKeyOk) {
        uint32_t ms = furi_ticks_to_ms(now - app->reaction_ready_at);
        snprintf(app->message, sizeof(app->message), "%lu ms - OK retry", (unsigned long)ms);
        app->reaction_state = 0;
    }
}

static void handle_snake(Arcade* app, InputKey key) {
    if(app->snake_over) {
        if(key == InputKeyOk) start_selected_game(app);
        return;
    }
    if(key == InputKeyUp && app->snake_dy != 1) { app->snake_dx = 0; app->snake_dy = -1; }
    else if(key == InputKeyDown && app->snake_dy != -1) { app->snake_dx = 0; app->snake_dy = 1; }
    else if(key == InputKeyLeft && app->snake_dx != 1) { app->snake_dx = -1; app->snake_dy = 0; }
    else if(key == InputKeyRight && app->snake_dx != -1) { app->snake_dx = 1; app->snake_dy = 0; }
    else if(key != InputKeyOk) return;
    snake_step(app);
}

static void handle_dice(Arcade* app, InputKey key) {
    if(key != InputKeyOk) return;
    app->player_die = 1 + random_range(6);
    app->flipper_die = 1 + random_range(6);
    if(app->player_die > app->flipper_die) set_message(app, "You win! OK again");
    else if(app->player_die < app->flipper_die) set_message(app, "Flipper wins!");
    else set_message(app, "Tie! OK again");
}

static void handle_memory(Arcade* app, InputKey key) {
    if(app->memory_over) {
        if(key == InputKeyOk) start_selected_game(app);
        return;
    }
    if(key == InputKeyUp) app->memory_selected = 0;
    else if(key == InputKeyRight) app->memory_selected = 1;
    else if(key == InputKeyDown) app->memory_selected = 2;
    else if(key == InputKeyLeft) app->memory_selected = 3;
    else if(key == InputKeyOk) {
        if(app->memory_selected != app->sequence[app->sequence_pos]) {
            app->memory_over = true;
        } else {
            app->sequence_pos++;
            if(app->sequence_pos >= app->sequence_length) {
                app->score++;
                app->sequence_pos = 0;
                if(app->sequence_length < MEMORY_MAX) app->sequence_length++;
                else set_message(app, "Perfect memory!");
            }
        }
    }
}

static void handle_treasure(Arcade* app, InputKey key) {
    if(app->treasure_won) {
        if(key == InputKeyOk) start_selected_game(app);
        return;
    }
    Point old = app->player;
    if(key == InputKeyUp && app->player.y > 0) app->player.y--;
    else if(key == InputKeyDown && app->player.y < 4) app->player.y++;
    else if(key == InputKeyLeft && app->player.x > 0) app->player.x--;
    else if(key == InputKeyRight && app->player.x < 7) app->player.x++;
    if(old.x != app->player.x || old.y != app->player.y) app->moves++;
    if(app->player.x == app->treasure.x && app->player.y == app->treasure.y)
        app->treasure_won = true;
}

static void handle_input(Arcade* app, InputKey key) {
    if(app->screen == ScreenMenu) {
        if(key == InputKeyUp) app->menu = (app->menu + 4) % 5;
        else if(key == InputKeyDown) app->menu = (app->menu + 1) % 5;
        else if(key == InputKeyOk) start_selected_game(app);
        else if(key == InputKeyBack) app->running = false;
        return;
    }
    if(key == InputKeyBack) {
        app->screen = ScreenMenu;
        return;
    }
    if(app->game == GameReaction) handle_reaction(app, key);
    else if(app->game == GameSnake) handle_snake(app, key);
    else if(app->game == GameDice) handle_dice(app, key);
    else if(app->game == GameMemory) handle_memory(app, key);
    else handle_treasure(app, key);
}

int32_t mini_arcade_app(void* p) {
    UNUSED(p);
    Arcade* app = malloc(sizeof(Arcade));
    app->queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->viewport = view_port_alloc();
    app->gui = furi_record_open(RECORD_GUI);
    app->running = true;
    app->screen = ScreenMenu;
    app->menu = 0;
    view_port_draw_callback_set(app->viewport, draw_callback, app);
    view_port_input_callback_set(app->viewport, input_callback, app);
    gui_add_view_port(app->gui, app->viewport, GuiLayerFullscreen);

    InputEvent event;
    while(app->running) {
        FuriStatus status = furi_message_queue_get(app->queue, &event, 25);
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if(app->screen == ScreenGame && app->game == GameReaction &&
           app->reaction_state == 1 && furi_get_tick() >= app->reaction_ready_at) {
            app->reaction_state = 2;
        }
        if(status == FuriStatusOk && (event.type == InputTypeShort || event.type == InputTypeRepeat))
            handle_input(app, event.key);
        furi_mutex_release(app->mutex);
        view_port_update(app->viewport);
    }

    gui_remove_view_port(app->gui, app->viewport);
    furi_record_close(RECORD_GUI);
    view_port_free(app->viewport);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
