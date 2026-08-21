#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_QUESTIONS 21
#define ARRAY_COUNT(x) (sizeof(x) / sizeof((x)[0]))

typedef enum {
    ScreenIntro,
    ScreenQuestion,
    ScreenGuess,
    ScreenResult,
} GameScreen;

typedef struct {
    const char* name;
    uint32_t traits;
} Thing;

enum {
    T_ALIVE = 1u << 0,
    T_ANIMAL = 1u << 1,
    T_PET = 1u << 2,
    T_FOOD = 1u << 3,
    T_ELECTRONIC = 1u << 4,
    T_INDOORS = 1u << 5,
    T_HANDHELD = 1u << 6,
    T_VEHICLE = 1u << 7,
    T_WATER = 1u << 8,
    T_FLY = 1u << 9,
    T_BIGGER_PERSON = 1u << 10,
    T_WEARABLE = 1u << 11,
    T_TOOL = 1u << 12,
    T_ROUND = 1u << 13,
    T_NATURE = 1u << 14,
};

static const char* questions[] = {
    "Is it alive?", "Is it an animal?", "Is it often a pet?", "Is it food?",
    "Is it electronic?", "Usually found indoors?", "Can you hold it?",
    "Is it a vehicle?", "Used in or on water?", "Can it fly?",
    "Bigger than a person?", "Can you wear it?", "Is it a tool?",
    "Is it mostly round?", "Found in nature?",
};

#define B(...) (__VA_ARGS__)
static const Thing things[] = {
    {"dog", B(T_ALIVE|T_ANIMAL|T_PET|T_INDOORS|T_NATURE)},
    {"cat", B(T_ALIVE|T_ANIMAL|T_PET|T_INDOORS|T_NATURE)},
    {"bird", B(T_ALIVE|T_ANIMAL|T_PET|T_FLY|T_NATURE)},
    {"fish", B(T_ALIVE|T_ANIMAL|T_PET|T_WATER|T_NATURE)},
    {"horse", B(T_ALIVE|T_ANIMAL|T_BIGGER_PERSON|T_NATURE)},
    {"shark", B(T_ALIVE|T_ANIMAL|T_WATER|T_BIGGER_PERSON|T_NATURE)},
    {"tree", B(T_ALIVE|T_BIGGER_PERSON|T_NATURE)},
    {"flower", B(T_ALIVE|T_HANDHELD|T_NATURE)},
    {"apple", B(T_FOOD|T_HANDHELD|T_ROUND|T_NATURE)},
    {"pizza", B(T_FOOD|T_HANDHELD|T_ROUND)},
    {"hamburger", B(T_FOOD|T_HANDHELD)},
    {"phone", B(T_ELECTRONIC|T_INDOORS|T_HANDHELD|T_TOOL)},
    {"computer", B(T_ELECTRONIC|T_INDOORS|T_TOOL)},
    {"television", B(T_ELECTRONIC|T_INDOORS)},
    {"camera", B(T_ELECTRONIC|T_HANDHELD|T_TOOL)},
    {"car", B(T_VEHICLE|T_BIGGER_PERSON)},
    {"bicycle", B(T_VEHICLE|T_BIGGER_PERSON|T_TOOL)},
    {"airplane", B(T_VEHICLE|T_FLY|T_BIGGER_PERSON)},
    {"boat", B(T_VEHICLE|T_WATER|T_BIGGER_PERSON)},
    {"hammer", B(T_INDOORS|T_HANDHELD|T_TOOL)},
    {"scissors", B(T_INDOORS|T_HANDHELD|T_TOOL)},
    {"watch", B(T_ELECTRONIC|T_HANDHELD|T_WEARABLE|T_TOOL|T_ROUND)},
    {"hat", B(T_INDOORS|T_HANDHELD|T_WEARABLE)},
    {"shoe", B(T_INDOORS|T_HANDHELD|T_WEARABLE)},
    {"ball", B(T_INDOORS|T_HANDHELD|T_ROUND)},
    {"moon", B(T_BIGGER_PERSON|T_ROUND|T_NATURE)},
    {"mountain", B(T_BIGGER_PERSON|T_NATURE)},
    {"key", B(T_INDOORS|T_HANDHELD|T_TOOL)},
    {"book", B(T_INDOORS|T_HANDHELD)},
    {"chair", B(T_INDOORS|T_TOOL)},
};

typedef struct {
    Gui* gui;
    ViewPort* viewport;
    FuriMessageQueue* queue;
    FuriMutex* mutex;
    GameScreen screen;
    bool active[ARRAY_COUNT(things)];
    uint32_t asked_traits;
    uint8_t question_number;
    int8_t current_trait;
    int16_t guess_index;
    bool won;
    bool running;
} Game;

static uint8_t active_count(Game* game) {
    uint8_t count = 0;
    for(size_t i = 0; i < ARRAY_COUNT(things); i++) count += game->active[i];
    return count;
}

static int16_t first_active(Game* game) {
    for(size_t i = 0; i < ARRAY_COUNT(things); i++) if(game->active[i]) return (int16_t)i;
    return -1;
}

static int8_t choose_question(Game* game) {
    uint8_t total = active_count(game);
    int8_t best = -1;
    uint8_t best_diff = 255;
    for(size_t trait = 0; trait < ARRAY_COUNT(questions); trait++) {
        uint32_t bit = 1u << trait;
        if(game->asked_traits & bit) continue;
        uint8_t yes = 0;
        for(size_t i = 0; i < ARRAY_COUNT(things); i++)
            if(game->active[i] && (things[i].traits & bit)) yes++;
        if(yes == 0 || yes == total) continue;
        uint8_t no = total - yes;
        uint8_t diff = yes > no ? yes - no : no - yes;
        if(diff < best_diff) { best_diff = diff; best = (int8_t)trait; }
    }
    return best;
}

static void start_game(Game* game) {
    for(size_t i = 0; i < ARRAY_COUNT(things); i++) game->active[i] = true;
    game->asked_traits = 0;
    game->question_number = 1;
    game->current_trait = choose_question(game);
    game->guess_index = -1;
    game->won = false;
    game->screen = ScreenQuestion;
}

static void make_guess(Game* game) {
    game->guess_index = first_active(game);
    game->screen = ScreenGuess;
}

static void answer_question(Game* game, int8_t answer) {
    if(game->current_trait < 0) { make_guess(game); return; }
    uint32_t bit = 1u << game->current_trait;
    game->asked_traits |= bit;
    if(answer != 0) {
        for(size_t i = 0; i < ARRAY_COUNT(things); i++) {
            if(!game->active[i]) continue;
            bool has = (things[i].traits & bit) != 0;
            if((answer > 0 && !has) || (answer < 0 && has)) game->active[i] = false;
        }
    }
    if(active_count(game) <= 1 || game->question_number >= MAX_QUESTIONS) {
        make_guess(game);
    } else {
        game->question_number++;
        game->current_trait = choose_question(game);
        if(game->current_trait < 0) make_guess(game);
    }
}

static void draw_center(Canvas* canvas, uint8_t y, const char* text) {
    canvas_draw_str_aligned(canvas, 64, y, AlignCenter, AlignCenter, text);
}

static void draw_callback(Canvas* canvas, void* context) {
    Game* game = context;
    furi_mutex_acquire(game->mutex, FuriWaitForever);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    draw_center(canvas, 8, "21 QUESTIONS");
    canvas_set_font(canvas, FontSecondary);
    if(game->screen == ScreenIntro) {
        draw_center(canvas, 25, "Think of an object");
        draw_center(canvas, 37, "I will try to guess it!");
        draw_center(canvas, 55, "OK = Start   BACK = Exit");
    } else if(game->screen == ScreenQuestion) {
        char status[32];
        snprintf(status, sizeof(status), "Question %u/21  Left:%u", game->question_number, active_count(game));
        draw_center(canvas, 22, status);
        draw_center(canvas, 36, questions[game->current_trait]);
        draw_center(canvas, 55, "LEFT No  OK ?  RIGHT Yes");
    } else if(game->screen == ScreenGuess) {
        draw_center(canvas, 24, "Is it...");
        canvas_set_font(canvas, FontPrimary);
        draw_center(canvas, 39, game->guess_index >= 0 ? things[game->guess_index].name : "something else?");
        canvas_set_font(canvas, FontSecondary);
        draw_center(canvas, 56, "LEFT No      RIGHT Yes");
    } else {
        canvas_set_font(canvas, FontPrimary);
        draw_center(canvas, 31, game->won ? "I GOT IT!" : "YOU STUMPED ME!");
        canvas_set_font(canvas, FontSecondary);
        draw_center(canvas, 52, "OK = Play again  BACK = Exit");
    }
    furi_mutex_release(game->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    Game* game = context;
    furi_message_queue_put(game->queue, event, 0);
}

int32_t twenty_one_questions_app(void* p) {
    UNUSED(p);
    Game* game = malloc(sizeof(Game));
    game->queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    game->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    game->viewport = view_port_alloc();
    game->gui = furi_record_open(RECORD_GUI);
    game->screen = ScreenIntro;
    game->running = true;
    view_port_draw_callback_set(game->viewport, draw_callback, game);
    view_port_input_callback_set(game->viewport, input_callback, game);
    gui_add_view_port(game->gui, game->viewport, GuiLayerFullscreen);

    InputEvent event;
    while(game->running) {
        if(furi_message_queue_get(game->queue, &event, FuriWaitForever) != FuriStatusOk) continue;
        if(event.type != InputTypeShort) continue;
        furi_mutex_acquire(game->mutex, FuriWaitForever);
        if(event.key == InputKeyBack) {
            game->running = false;
        } else if(game->screen == ScreenIntro && event.key == InputKeyOk) {
            start_game(game);
        } else if(game->screen == ScreenQuestion) {
            if(event.key == InputKeyLeft) answer_question(game, -1);
            else if(event.key == InputKeyRight) answer_question(game, 1);
            else if(event.key == InputKeyOk) answer_question(game, 0);
        } else if(game->screen == ScreenGuess) {
            if(event.key == InputKeyRight) { game->won = true; game->screen = ScreenResult; }
            else if(event.key == InputKeyLeft) { game->won = false; game->screen = ScreenResult; }
        } else if(game->screen == ScreenResult && event.key == InputKeyOk) {
            start_game(game);
        }
        furi_mutex_release(game->mutex);
        view_port_update(game->viewport);
    }

    gui_remove_view_port(game->gui, game->viewport);
    furi_record_close(RECORD_GUI);
    view_port_free(game->viewport);
    furi_message_queue_free(game->queue);
    furi_mutex_free(game->mutex);
    free(game);
    return 0;
}
