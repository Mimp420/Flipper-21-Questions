#include <furi.h>
#include <furi_hal_random.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum { ScreenHome, ScreenActions, ScreenMessage } PetScreen;

typedef struct {
    Gui* gui;
    ViewPort* viewport;
    FuriMessageQueue* queue;
    FuriMutex* mutex;
    bool running;
    PetScreen screen;
    uint8_t selected;
    uint8_t fullness;
    uint8_t happiness;
    uint8_t energy;
    uint16_t coins;
    uint16_t fetches;
    uint32_t last_decay;
    char message[32];
} Pet;

static const char* actions[] = {"Feed (-2)", "Play fetch", "Rest", "Treat (-5)"};

static uint8_t clamp_add(uint8_t value, int8_t amount) {
    int16_t result = (int16_t)value + amount;
    if(result < 0) return 0;
    if(result > 100) return 100;
    return (uint8_t)result;
}

static void center(Canvas* canvas, uint8_t y, const char* text) {
    canvas_draw_str_aligned(canvas, 64, y, AlignCenter, AlignCenter, text);
}

static void draw_bar(Canvas* canvas, uint8_t x, uint8_t y, uint8_t width, uint8_t value) {
    canvas_draw_frame(canvas, x, y, width, 6);
    uint8_t fill = ((width - 2) * value) / 100;
    if(fill) canvas_draw_box(canvas, x + 1, y + 1, fill, 4);
}

static void draw_russell(Canvas* canvas, Pet* pet) {
    bool happy = pet->happiness >= 35 && pet->fullness >= 25;
    bool tired = pet->energy < 25;

    canvas_draw_line(canvas, 48, 18, 55, 7);
    canvas_draw_line(canvas, 55, 7, 59, 21);
    canvas_draw_line(canvas, 59, 21, 48, 18);
    canvas_draw_line(canvas, 69, 21, 73, 7);
    canvas_draw_line(canvas, 73, 7, 80, 18);
    canvas_draw_line(canvas, 80, 18, 69, 21);
    canvas_draw_rframe(canvas, 51, 15, 27, 24, 7);
    canvas_draw_box(canvas, 47, 24, 5, 8);
    canvas_draw_box(canvas, 77, 24, 5, 8);

    if(tired) {
        canvas_draw_line(canvas, 56, 25, 61, 25);
        canvas_draw_line(canvas, 68, 25, 73, 25);
    } else {
        canvas_draw_disc(canvas, 59, 25, 1);
        canvas_draw_disc(canvas, 71, 25, 1);
    }
    canvas_draw_disc(canvas, 65, 30, 2);
    if(happy) {
        canvas_draw_line(canvas, 60, 34, 64, 36);
        canvas_draw_line(canvas, 64, 36, 69, 34);
        canvas_draw_line(canvas, 69, 34, 73, 36);
    } else {
        canvas_draw_line(canvas, 61, 36, 65, 34);
        canvas_draw_line(canvas, 65, 34, 70, 36);
    }

    canvas_draw_rframe(canvas, 46, 38, 38, 16, 6);
    canvas_draw_line(canvas, 52, 51, 50, 58);
    canvas_draw_line(canvas, 59, 52, 59, 59);
    canvas_draw_line(canvas, 73, 52, 73, 59);
    canvas_draw_line(canvas, 80, 50, 83, 57);
    canvas_draw_line(canvas, 83, 57, 89, 54);
}

static const char* mood_text(Pet* pet) {
    if(pet->energy < 20) return "Russell is sleepy";
    if(pet->fullness < 20) return "Russell is hungry";
    if(pet->happiness < 25) return "Russell wants to play";
    if(pet->happiness > 80) return "Russell loves you!";
    return "Russell is happy";
}

static void draw_home(Canvas* canvas, Pet* pet) {
    char coins[20];
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "POCKET RUSSELL");
    snprintf(coins, sizeof(coins), "$%u", pet->coins);
    canvas_draw_str_aligned(canvas, 126, 9, AlignRight, AlignBottom, coins);

    draw_russell(canvas, pet);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 19, "FOOD");
    draw_bar(canvas, 2, 22, 36, pet->fullness);
    canvas_draw_str(canvas, 2, 36, "HAPPY");
    draw_bar(canvas, 2, 39, 36, pet->happiness);
    canvas_draw_str(canvas, 2, 53, "ENERGY");
    draw_bar(canvas, 2, 56, 36, pet->energy);
    center(canvas, 62, "OK Actions");
}

static void draw_actions(Canvas* canvas, Pet* pet) {
    canvas_set_font(canvas, FontPrimary);
    center(canvas, 9, "CARE FOR RUSSELL");
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t row = 0; row < 4; row++) {
        uint8_t y = 20 + row * 11;
        if(row == pet->selected) {
            canvas_draw_rframe(canvas, 19, y - 8, 90, 11, 2);
            canvas_set_font(canvas, FontPrimary);
        }
        center(canvas, y, actions[row]);
        canvas_set_font(canvas, FontSecondary);
    }
    center(canvas, 63, "OK Select   BACK Home");
}

static void draw_message(Canvas* canvas, Pet* pet) {
    canvas_set_font(canvas, FontPrimary);
    center(canvas, 10, "POCKET RUSSELL");
    draw_russell(canvas, pet);
    canvas_set_font(canvas, FontSecondary);
    center(canvas, 59, pet->message);
}

static void draw_callback(Canvas* canvas, void* context) {
    Pet* pet = context;
    furi_mutex_acquire(pet->mutex, FuriWaitForever);
    canvas_clear(canvas);
    if(pet->screen == ScreenHome) draw_home(canvas, pet);
    else if(pet->screen == ScreenActions) draw_actions(canvas, pet);
    else draw_message(canvas, pet);
    furi_mutex_release(pet->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    Pet* pet = context;
    furi_message_queue_put(pet->queue, event, 0);
}

static void show_message(Pet* pet, const char* text) {
    snprintf(pet->message, sizeof(pet->message), "%s", text);
    pet->screen = ScreenMessage;
}

static void perform_action(Pet* pet) {
    if(pet->selected == 0) {
        if(pet->coins < 2) show_message(pet, "Not enough coins!");
        else {
            pet->coins -= 2;
            pet->fullness = clamp_add(pet->fullness, 25);
            pet->happiness = clamp_add(pet->happiness, 3);
            show_message(pet, "Yum! Russell ate.");
        }
    } else if(pet->selected == 1) {
        if(pet->energy < 10) show_message(pet, "Too tired to fetch!");
        else {
            uint8_t found = 1 + (furi_hal_random_get() % 4);
            pet->coins += found;
            pet->fetches++;
            pet->energy = clamp_add(pet->energy, -12);
            pet->fullness = clamp_add(pet->fullness, -5);
            pet->happiness = clamp_add(pet->happiness, 18);
            snprintf(pet->message, sizeof(pet->message), "Fetch! Found %u coins", found);
            pet->screen = ScreenMessage;
        }
    } else if(pet->selected == 2) {
        pet->energy = clamp_add(pet->energy, 35);
        pet->fullness = clamp_add(pet->fullness, -5);
        show_message(pet, "Russell took a nap.");
    } else {
        if(pet->coins < 5) show_message(pet, "Treats cost 5 coins");
        else {
            pet->coins -= 5;
            pet->fullness = clamp_add(pet->fullness, 12);
            pet->happiness = clamp_add(pet->happiness, 28);
            show_message(pet, "Best treat ever!");
        }
    }
}

static void handle_input(Pet* pet, InputKey key) {
    if(pet->screen == ScreenHome) {
        if(key == InputKeyOk) pet->screen = ScreenActions;
        else if(key == InputKeyBack) pet->running = false;
    } else if(pet->screen == ScreenActions) {
        if(key == InputKeyUp) pet->selected = (pet->selected + 3) % 4;
        else if(key == InputKeyDown) pet->selected = (pet->selected + 1) % 4;
        else if(key == InputKeyOk) perform_action(pet);
        else if(key == InputKeyBack) pet->screen = ScreenHome;
    } else {
        if(key == InputKeyOk || key == InputKeyBack) pet->screen = ScreenHome;
    }
}

static void decay_stats(Pet* pet) {
    uint32_t now = furi_get_tick();
    if(now - pet->last_decay >= furi_ms_to_ticks(15000)) {
        pet->last_decay = now;
        pet->fullness = clamp_add(pet->fullness, -1);
        pet->happiness = clamp_add(pet->happiness, -1);
        pet->energy = clamp_add(pet->energy, -1);
        if(pet->fullness == 0) pet->happiness = clamp_add(pet->happiness, -2);
    }
}

int32_t pocket_russell_app(void* p) {
    UNUSED(p);
    Pet* pet = malloc(sizeof(Pet));
    pet->queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    pet->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    pet->viewport = view_port_alloc();
    pet->gui = furi_record_open(RECORD_GUI);
    pet->running = true;
    pet->screen = ScreenHome;
    pet->selected = 0;
    pet->fullness = 75;
    pet->happiness = 80;
    pet->energy = 70;
    pet->coins = 10;
    pet->fetches = 0;
    pet->last_decay = furi_get_tick();
    snprintf(pet->message, sizeof(pet->message), "%s", mood_text(pet));

    view_port_draw_callback_set(pet->viewport, draw_callback, pet);
    view_port_input_callback_set(pet->viewport, input_callback, pet);
    gui_add_view_port(pet->gui, pet->viewport, GuiLayerFullscreen);

    InputEvent event;
    while(pet->running) {
        FuriStatus status = furi_message_queue_get(pet->queue, &event, 100);
        furi_mutex_acquire(pet->mutex, FuriWaitForever);
        decay_stats(pet);
        if(status == FuriStatusOk && event.type == InputTypeShort) handle_input(pet, event.key);
        furi_mutex_release(pet->mutex);
        view_port_update(pet->viewport);
    }

    gui_remove_view_port(pet->gui, pet->viewport);
    furi_record_close(RECORD_GUI);
    view_port_free(pet->viewport);
    furi_message_queue_free(pet->queue);
    furi_mutex_free(pet->mutex);
    free(pet);
    return 0;
}
