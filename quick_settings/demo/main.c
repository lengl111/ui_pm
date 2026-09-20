#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <lvgl.h>

#include "quick_settings/quick_settings.h"

#define QUICK_DEMO_WIDTH 800
#define QUICK_DEMO_HEIGHT 480
#define QUICK_DEMO_INPUT_CAPACITY 64u

typedef enum {
    QUICK_DEMO_INPUT_TOUCH = 0,
    QUICK_DEMO_INPUT_TOGGLE,
    QUICK_DEMO_INPUT_CLOSE,
} quick_demo_input_kind_t;

typedef struct {
    quick_demo_input_kind_t kind;
    quick_settings_touch_t touch;
} quick_demo_input_t;

typedef struct {
    lv_display_t *display;
    quick_settings_t *settings;
    SDL_mutex *input_mutex;
    quick_demo_input_t input_queue[QUICK_DEMO_INPUT_CAPACITY];
    SDL_FingerID finger_ids[2];
    bool finger_active[2];
    bool control_drag_active;
    uint32_t window_id;
    uint8_t input_read;
    uint8_t input_write;
    uint8_t opened_count;
    uint8_t closed_count;
    uint8_t wifi_count;
    uint8_t brightness_count;
    uint8_t volume_count;
    quick_settings_close_reason_t last_close_reason;
} quick_demo_t;

static void quick_demo_queue_locked(
    quick_demo_t *demo, const quick_demo_input_t *input)
{
    uint8_t next = (uint8_t)((demo->input_write + 1u) %
                             QUICK_DEMO_INPUT_CAPACITY);

    if(next == demo->input_read) return;
    demo->input_queue[demo->input_write] = *input;
    demo->input_write = next;
}

static void quick_demo_queue_touch_locked(
    quick_demo_t *demo,
    uint8_t id,
    int32_t x,
    int32_t y,
    bool pressed)
{
    const quick_demo_input_t input = {
        .kind = QUICK_DEMO_INPUT_TOUCH,
        .touch = {
            .id = id,
            .x = x,
            .y = y,
            .pressed = pressed,
        },
    };

    quick_demo_queue_locked(demo, &input);
}

static int quick_demo_finger_slot(
    quick_demo_t *demo, SDL_FingerID finger_id, bool allocate)
{
    for(uint8_t index = 0u; index < 2u; ++index) {
        if(demo->finger_active[index] &&
           demo->finger_ids[index] == finger_id) {
            return (int)index;
        }
    }
    if(!allocate) return -1;
    for(uint8_t index = 0u; index < 2u; ++index) {
        if(!demo->finger_active[index]) {
            demo->finger_active[index] = true;
            demo->finger_ids[index] = finger_id;
            return (int)index;
        }
    }
    return -1;
}

static int SDLCALL quick_demo_watch_event(void *user_data, SDL_Event *event)
{
    quick_demo_t *demo = user_data;
    quick_demo_input_t input;
    uint32_t event_window = 0u;

    if(event->type == SDL_MOUSEBUTTONDOWN ||
       event->type == SDL_MOUSEBUTTONUP) {
        event_window = event->button.windowID;
    }
    else if(event->type == SDL_MOUSEMOTION) {
        event_window = event->motion.windowID;
    }
    else if(event->type == SDL_KEYDOWN) {
        event_window = event->key.windowID;
    }
    else if(event->type == SDL_FINGERDOWN ||
            event->type == SDL_FINGERMOTION ||
            event->type == SDL_FINGERUP) {
#if SDL_VERSION_ATLEAST(2, 0, 12)
        event_window = event->tfinger.windowID;
#else
        event_window = demo->window_id;
#endif
    }
    else {
        return 1;
    }
    if(event_window != demo->window_id) return 1;

    SDL_LockMutex(demo->input_mutex);
    if(event->type == SDL_KEYDOWN && event->key.repeat == 0) {
        if(event->key.keysym.sym == SDLK_q) {
            input.kind = QUICK_DEMO_INPUT_TOGGLE;
            quick_demo_queue_locked(demo, &input);
        }
        else if(event->key.keysym.sym == SDLK_ESCAPE) {
            input.kind = QUICK_DEMO_INPUT_CLOSE;
            quick_demo_queue_locked(demo, &input);
        }
    }
    else if(event->type == SDL_FINGERDOWN ||
            event->type == SDL_FINGERMOTION ||
            event->type == SDL_FINGERUP) {
        bool pressed = event->type != SDL_FINGERUP;
        int slot = quick_demo_finger_slot(
            demo, event->tfinger.fingerId, event->type == SDL_FINGERDOWN);

        if(slot >= 0) {
            quick_demo_queue_touch_locked(
                demo,
                (uint8_t)slot,
                (int32_t)(event->tfinger.x * QUICK_DEMO_WIDTH),
                (int32_t)(event->tfinger.y * QUICK_DEMO_HEIGHT),
                pressed);
            if(!pressed) demo->finger_active[slot] = false;
        }
    }
    else if(event->type == SDL_MOUSEBUTTONDOWN &&
            event->button.button == SDL_BUTTON_LEFT &&
            (SDL_GetModState() & KMOD_CTRL) != 0) {
        demo->control_drag_active = true;
        quick_demo_queue_touch_locked(
            demo, 0u, event->button.x - 42, event->button.y, true);
        quick_demo_queue_touch_locked(
            demo, 1u, event->button.x + 42, event->button.y, true);
    }
    else if(event->type == SDL_MOUSEMOTION &&
            demo->control_drag_active) {
        quick_demo_queue_touch_locked(
            demo, 0u, event->motion.x - 42, event->motion.y, true);
        quick_demo_queue_touch_locked(
            demo, 1u, event->motion.x + 42, event->motion.y, true);
    }
    else if(event->type == SDL_MOUSEBUTTONUP &&
            event->button.button == SDL_BUTTON_LEFT &&
            demo->control_drag_active) {
        quick_demo_queue_touch_locked(
            demo, 0u, event->button.x - 42, event->button.y, false);
        quick_demo_queue_touch_locked(
            demo, 1u, event->button.x + 42, event->button.y, false);
        demo->control_drag_active = false;
    }
    SDL_UnlockMutex(demo->input_mutex);
    return 1;
}

static void quick_demo_drain_input(quick_demo_t *demo)
{
    quick_demo_input_t pending[QUICK_DEMO_INPUT_CAPACITY];
    uint8_t count = 0u;

    SDL_LockMutex(demo->input_mutex);
    while(demo->input_read != demo->input_write &&
          count < QUICK_DEMO_INPUT_CAPACITY) {
        pending[count++] = demo->input_queue[demo->input_read];
        demo->input_read = (uint8_t)((demo->input_read + 1u) %
                                     QUICK_DEMO_INPUT_CAPACITY);
    }
    SDL_UnlockMutex(demo->input_mutex);

    for(uint8_t index = 0u; index < count; ++index) {
        if(pending[index].kind == QUICK_DEMO_INPUT_TOUCH) {
            quick_settings_gesture_feed(
                demo->settings, &pending[index].touch, 1u);
        }
        else if(pending[index].kind == QUICK_DEMO_INPUT_TOGGLE) {
            quick_settings_toggle(demo->settings, LV_ANIM_ON);
        }
        else if(quick_settings_is_open(demo->settings)) {
            quick_settings_hide(
                demo->settings,
                LV_ANIM_ON,
                QUICK_SETTINGS_CLOSE_PROGRAMMATIC);
        }
    }
}

static void quick_demo_pump(quick_demo_t *demo, uint32_t duration_ms)
{
    uint32_t start = lv_tick_get();

    do {
        quick_demo_drain_input(demo);
        (void)lv_timer_handler();
        quick_demo_drain_input(demo);
        lv_refr_now(demo->display);
        SDL_Delay(2u);
    } while(lv_tick_elaps(start) < duration_ms);
}

static bool quick_demo_push_mouse_button(
    quick_demo_t *demo, uint32_t type, int32_t x, int32_t y)
{
    SDL_Event event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.button.windowID = demo->window_id;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.state =
        type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    event.button.clicks = 1u;
    event.button.x = x;
    event.button.y = y;
    return SDL_PushEvent(&event) == 1;
}

static bool quick_demo_push_mouse_motion(
    quick_demo_t *demo,
    int32_t x,
    int32_t y,
    int32_t relative_x,
    int32_t relative_y)
{
    SDL_Event event;

    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEMOTION;
    event.motion.windowID = demo->window_id;
    event.motion.state = SDL_BUTTON_LMASK;
    event.motion.x = x;
    event.motion.y = y;
    event.motion.xrel = relative_x;
    event.motion.yrel = relative_y;
    return SDL_PushEvent(&event) == 1;
}

static bool quick_demo_click(
    quick_demo_t *demo, int32_t x, int32_t y)
{
    if(!quick_demo_push_mouse_button(
           demo, SDL_MOUSEBUTTONDOWN, x, y)) {
        return false;
    }
    quick_demo_pump(demo, 20u);
    if(!quick_demo_push_mouse_button(
           demo, SDL_MOUSEBUTTONUP, x, y)) {
        return false;
    }
    quick_demo_pump(demo, 50u);
    return true;
}

static bool quick_demo_drag(
    quick_demo_t *demo,
    int32_t start_x,
    int32_t start_y,
    int32_t end_x,
    int32_t end_y)
{
    int32_t previous_x = start_x;
    int32_t previous_y = start_y;

    if(!quick_demo_push_mouse_button(
           demo, SDL_MOUSEBUTTONDOWN, start_x, start_y)) {
        return false;
    }
    quick_demo_pump(demo, 20u);
    for(int32_t step = 1; step <= 6; ++step) {
        int32_t x = start_x + (end_x - start_x) * step / 6;
        int32_t y = start_y + (end_y - start_y) * step / 6;

        if(!quick_demo_push_mouse_motion(
               demo, x, y, x - previous_x, y - previous_y)) {
            return false;
        }
        quick_demo_pump(demo, 12u);
        previous_x = x;
        previous_y = y;
    }
    if(!quick_demo_push_mouse_button(
           demo, SDL_MOUSEBUTTONUP, end_x, end_y)) {
        return false;
    }
    quick_demo_pump(demo, 320u);
    return true;
}

static void quick_demo_on_event(
    void *user_ctx, const quick_settings_event_t *event)
{
    quick_demo_t *demo = user_ctx;

    switch(event->kind) {
        case QUICK_SETTINGS_EVENT_OPENED:
            ++demo->opened_count;
            printf("quick settings opened\n");
            break;
        case QUICK_SETTINGS_EVENT_CLOSED:
            ++demo->closed_count;
            demo->last_close_reason = event->close_reason;
            printf("quick settings closed\n");
            break;
        case QUICK_SETTINGS_EVENT_WIFI_CHANGED:
            ++demo->wifi_count;
            printf("wifi %s\n", event->enabled ? "on" : "off");
            break;
        case QUICK_SETTINGS_EVENT_BRIGHTNESS_CHANGED:
            ++demo->brightness_count;
            printf("brightness %d\n", (int)event->value);
            break;
        case QUICK_SETTINGS_EVENT_VOLUME_CHANGED:
            ++demo->volume_count;
            printf("volume %d\n", (int)event->value);
            break;
    }
}

static lv_obj_t *quick_demo_make_card(
    lv_obj_t *parent,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    lv_color_t color,
    const char *symbol,
    const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_t *icon;
    lv_obj_t *label;

    if(card == NULL) return NULL;
    lv_obj_set_size(card, width, height);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_bg_color(card, color, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    icon = lv_label_create(card);
    label = lv_label_create(card);
    if(icon == NULL || label == NULL) return NULL;
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 4, 2);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 4, -2);
    return card;
}

static bool quick_demo_create_background(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *status_bar;
    lv_obj_t *time;
    lv_obj_t *status;
    lv_obj_t *heading;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B0D10), 0);
    status_bar = lv_obj_create(screen);
    if(status_bar == NULL) return false;
    lv_obj_remove_style_all(status_bar);
    lv_obj_set_size(status_bar, LV_PCT(100), 60);
    lv_obj_set_pos(status_bar, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x202327), 0);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_COVER, 0);
    lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    time = lv_label_create(status_bar);
    status = lv_label_create(status_bar);
    if(time == NULL || status == NULL) return false;
    lv_label_set_text(time, "12:00  Monday");
    lv_obj_set_style_text_font(time, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(time, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(time, LV_ALIGN_LEFT_MID, 20, 0);
    lv_label_set_text(
        status, LV_SYMBOL_WIFI "  " LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, -20, 0);

    heading = lv_label_create(screen);
    if(heading == NULL) return false;
    lv_label_set_text(heading, "HOME");
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(0xA9B0B8), 0);
    lv_obj_set_pos(heading, 28, 82);

    return quick_demo_make_card(
               screen, 28, 126, 270, 142, lv_color_hex(0xD04A63),
               LV_SYMBOL_IMAGE, "Camera") != NULL &&
           quick_demo_make_card(
               screen, 312, 126, 222, 142, lv_color_hex(0x2A78CB),
               LV_SYMBOL_AUDIO, "Assistant") != NULL &&
           quick_demo_make_card(
               screen, 548, 126, 224, 142, lv_color_hex(0x338A61),
               LV_SYMBOL_SETTINGS, "Settings") != NULL &&
           quick_demo_make_card(
               screen, 28, 282, 365, 160, lv_color_hex(0x9B6A20),
               LV_SYMBOL_DIRECTORY, "Gallery") != NULL &&
           quick_demo_make_card(
               screen, 407, 282, 365, 160, lv_color_hex(0x6653A8),
               LV_SYMBOL_PLAY, "Media") != NULL;
}

static bool quick_demo_init(quick_demo_t *demo)
{
    SDL_Renderer *renderer;
    SDL_Window *window;
    lv_indev_t *mouse;
    lv_indev_t *mousewheel;
    lv_indev_t *keyboard;
    const quick_settings_config_t config = {
        .panel_height = 340u,
        .edge_start_height = 56u,
        .open_distance = 96u,
        .close_distance = 72u,
        .animation_duration_ms = 240u,
        .title_font = &lv_font_montserrat_24,
        .body_font = &lv_font_montserrat_18,
        .symbol_font = &lv_font_montserrat_32,
        .on_event = quick_demo_on_event,
        .user_ctx = demo,
    };

    memset(demo, 0, sizeof(*demo));
    demo->display = lv_sdl_window_create(
        QUICK_DEMO_WIDTH, QUICK_DEMO_HEIGHT);
    if(demo->display == NULL) return false;
    lv_sdl_window_set_title(demo->display, "Quick settings overlay");
    lv_display_set_default(demo->display);
    renderer = lv_sdl_window_get_renderer(demo->display);
    window = renderer != NULL ? SDL_RenderGetWindow(renderer) : NULL;
    if(window == NULL) return false;
    demo->window_id = SDL_GetWindowID(window);

    mouse = lv_sdl_mouse_create();
    mousewheel = lv_sdl_mousewheel_create();
    keyboard = lv_sdl_keyboard_create();
    if(mouse == NULL || mousewheel == NULL || keyboard == NULL) return false;
    lv_indev_set_display(mouse, demo->display);
    lv_indev_set_display(mousewheel, demo->display);
    lv_indev_set_display(keyboard, demo->display);

    if(!quick_demo_create_background()) return false;
    demo->settings = quick_settings_create(lv_screen_active(), &config);
    if(demo->settings == NULL) return false;
    demo->input_mutex = SDL_CreateMutex();
    if(demo->input_mutex == NULL) return false;
    SDL_AddEventWatch(quick_demo_watch_event, demo);
    return true;
}

static void quick_demo_feed(
    quick_demo_t *demo,
    uint8_t id,
    int32_t x,
    int32_t y,
    bool pressed)
{
    const quick_settings_touch_t touch = {
        .id = id,
        .x = x,
        .y = y,
        .pressed = pressed,
    };

    quick_settings_gesture_feed(demo->settings, &touch, 1u);
}

static void quick_demo_release_pair(
    quick_demo_t *demo, int32_t first_x, int32_t second_x, int32_t y)
{
    quick_demo_feed(demo, 0u, first_x, y, false);
    quick_demo_feed(demo, 1u, second_x, y, false);
}

static int quick_demo_run_smoke(quick_demo_t *demo)
{
    quick_settings_state_t state;
    uint8_t brightness_before;
    uint8_t volume_before;

    if(quick_settings_is_open(demo->settings)) return 1;

    quick_demo_feed(demo, 0u, 200, 20, true);
    quick_demo_feed(demo, 0u, 200, 180, true);
    quick_demo_feed(demo, 0u, 200, 180, false);
    if(quick_settings_is_open(demo->settings)) return 1;

    quick_demo_feed(demo, 0u, 200, 90, true);
    quick_demo_feed(demo, 1u, 400, 90, true);
    quick_demo_feed(demo, 0u, 200, 230, true);
    quick_demo_feed(demo, 1u, 400, 230, true);
    quick_demo_release_pair(demo, 200, 400, 230);
    if(quick_settings_is_open(demo->settings)) return 1;

    quick_demo_feed(demo, 0u, 200, 20, true);
    quick_demo_feed(demo, 1u, 400, 24, true);
    quick_demo_feed(demo, 0u, 340, 36, true);
    quick_demo_feed(demo, 1u, 540, 40, true);
    quick_demo_release_pair(demo, 340, 540, 40);
    if(quick_settings_is_open(demo->settings)) return 1;

    quick_demo_feed(demo, 0u, 200, 20, true);
    quick_demo_feed(demo, 1u, 400, 24, true);
    quick_demo_feed(demo, 0u, 202, 132, true);
    quick_demo_feed(demo, 1u, 398, 136, true);
    if(!quick_settings_is_open(demo->settings) ||
       demo->opened_count != 1u) {
        return 1;
    }
    quick_demo_release_pair(demo, 202, 398, 136);
    quick_demo_pump(demo, 300u);

    quick_settings_get_state(demo->settings, &state);
    if(!state.wifi_enabled) return 1;
    if(!quick_demo_click(demo, 120, 150)) return 1;
    quick_settings_get_state(demo->settings, &state);
    if(state.wifi_enabled || demo->wifi_count != 1u) return 1;

    brightness_before = demo->brightness_count;
    if(!quick_demo_click(demo, 310, 244)) return 1;
    quick_settings_get_state(demo->settings, &state);
    if(demo->brightness_count <= brightness_before ||
       state.brightness >= 72) {
        return 1;
    }

    volume_before = demo->volume_count;
    if(!quick_demo_click(demo, 600, 304)) return 1;
    quick_settings_get_state(demo->settings, &state);
    if(demo->volume_count <= volume_before || state.volume <= 46) return 1;

    if(!quick_demo_click(demo, 400, 430)) return 1;
    quick_demo_pump(demo, 260u);
    if(quick_settings_is_open(demo->settings) ||
       demo->closed_count != 1u ||
       demo->last_close_reason != QUICK_SETTINGS_CLOSE_BACKDROP) {
        return 1;
    }

    quick_settings_set_wifi(demo->settings, true);
    quick_settings_set_brightness(demo->settings, 66);
    quick_settings_set_volume(demo->settings, 37);
    quick_settings_set_wifi_name(demo->settings, "Lab network");
    quick_settings_get_state(demo->settings, &state);
    if(!state.wifi_enabled || state.brightness != 66 ||
       state.volume != 37 || strcmp(state.wifi_name, "Lab network") != 0) {
        return 1;
    }

    quick_settings_show(demo->settings, LV_ANIM_OFF);
    if(!quick_demo_drag(demo, 780, 320, 780, 220)) return 1;
    if(quick_settings_is_open(demo->settings) ||
       demo->closed_count != 2u ||
       demo->last_close_reason != QUICK_SETTINGS_CLOSE_SWIPE) {
        return 1;
    }
    printf("quick settings smoke passed\n");
    return 0;
}

static void quick_demo_write_u16(FILE *file, uint16_t value)
{
    const uint8_t bytes[2] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
    };
    (void)fwrite(bytes, sizeof(bytes), 1u, file);
}

static void quick_demo_write_u32(FILE *file, uint32_t value)
{
    const uint8_t bytes[4] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 24),
    };
    (void)fwrite(bytes, sizeof(bytes), 1u, file);
}

static bool quick_demo_write_snapshot(
    quick_demo_t *demo, const char *path)
{
    lv_draw_buf_t *snapshot;
    FILE *file;
    uint32_t row_size;
    uint32_t image_size;

    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(demo->display);
    snapshot = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB888);
    if(snapshot == NULL) return false;
    file = fopen(path, "wb");
    if(file == NULL) {
        lv_draw_buf_destroy(snapshot);
        return false;
    }
    row_size = (snapshot->header.w * 3u + 3u) & ~UINT32_C(3);
    image_size = row_size * snapshot->header.h;
    (void)fwrite("BM", 2u, 1u, file);
    quick_demo_write_u32(file, UINT32_C(54) + image_size);
    quick_demo_write_u16(file, 0u);
    quick_demo_write_u16(file, 0u);
    quick_demo_write_u32(file, 54u);
    quick_demo_write_u32(file, 40u);
    quick_demo_write_u32(file, snapshot->header.w);
    quick_demo_write_u32(file, snapshot->header.h);
    quick_demo_write_u16(file, 1u);
    quick_demo_write_u16(file, 24u);
    quick_demo_write_u32(file, 0u);
    quick_demo_write_u32(file, image_size);
    quick_demo_write_u32(file, 2835u);
    quick_demo_write_u32(file, 2835u);
    quick_demo_write_u32(file, 0u);
    quick_demo_write_u32(file, 0u);
    for(int32_t y = (int32_t)snapshot->header.h - 1; y >= 0; --y) {
        const uint8_t *row =
            snapshot->data + (uint32_t)y * snapshot->header.stride;
        uint32_t x;

        for(x = 0u; x < snapshot->header.w; ++x) {
            const uint8_t bgr[3] = {
                row[x * 3u],
                row[x * 3u + 1u],
                row[x * 3u + 2u],
            };
            (void)fwrite(bgr, sizeof(bgr), 1u, file);
        }
        for(x = snapshot->header.w * 3u; x < row_size; ++x) {
            (void)fputc(0, file);
        }
    }
    (void)fclose(file);
    lv_draw_buf_destroy(snapshot);
    return true;
}

int main(int argc, char **argv)
{
    bool run_smoke = argc == 2 && strcmp(argv[1], "--smoke") == 0;
    bool write_screenshot =
        argc == 3 && strcmp(argv[1], "--screenshot") == 0;
    quick_demo_t demo;

    if(argc > 3 || (argc == 2 && !run_smoke) ||
       (argc == 3 && !write_screenshot)) {
        fprintf(
            stderr,
            "usage: quick_settings_demo [--smoke | --screenshot FILE]\n");
        return 2;
    }
    lv_init();
    if(!quick_demo_init(&demo)) {
        fprintf(stderr, "failed to initialize quick settings demo\n");
        return 1;
    }
    quick_demo_pump(&demo, 30u);
    if(run_smoke) return quick_demo_run_smoke(&demo);
    if(write_screenshot) {
        quick_settings_show(demo.settings, LV_ANIM_OFF);
        quick_demo_pump(&demo, 30u);
        return quick_demo_write_snapshot(&demo, argv[2]) ? 0 : 1;
    }
    while(true) {
        quick_demo_drain_input(&demo);
        (void)lv_timer_handler();
        quick_demo_drain_input(&demo);
        SDL_Delay(5u);
    }
}
