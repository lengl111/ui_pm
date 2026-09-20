#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <lvgl.h>

#include "paged_list/paged_list.h"

#define DEMO_WIDTH 800
#define DEMO_HEIGHT 480
#define DEMO_FILE_COUNT 1800u
#define DEMO_VISIBLE_ROWS 6u

typedef struct {
    lv_display_t *display;
    paged_list_t *list;
    lv_obj_t *page_label;
    lv_obj_t *status_label;
    lv_group_t *group;
    bool cached[DEMO_FILE_COUNT];
    uint32_t pending_generation;
    uint32_t pending_first;
    uint32_t pending_last;
    bool load_scheduled;
} demo_t;

static void demo_update_labels(demo_t *demo)
{
    uint32_t page = paged_list_current_page(demo->list);
    uint32_t pages = paged_list_page_count(demo->list);

    lv_label_set_text_fmt(demo->page_label, "Page %u / %u",
                          (unsigned)(page + 1u), (unsigned)pages);
    lv_label_set_text_fmt(demo->status_label, "Selected file %04u",
                          (unsigned)(paged_list_selected_index(demo->list) + 1u));
}

static lv_obj_t *demo_create_row(void *user_ctx, lv_obj_t *parent)
{
    (void)user_ctx;
    lv_obj_t *row = lv_button_create(parent);
    lv_obj_t *number = lv_label_create(row);
    lv_obj_t *name = lv_label_create(row);

    lv_obj_set_style_radius(row, 5, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1B2735), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x31465E), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x176B87), LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(row, 14, 0);
    lv_obj_set_style_text_font(row, &lv_font_montserrat_16, 0);
    lv_obj_set_width(number, 72);
    lv_obj_set_style_text_color(number, lv_color_hex(0x7FA7C8), 0);
    lv_obj_align(number, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_width(name, 510);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(name, lv_color_hex(0xE6EEF6), 0);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 82, 0);
    return row;
}

static bool demo_bind_row(
    void *user_ctx, lv_obj_t *row, uint32_t index, bool selected)
{
    demo_t *demo = user_ctx;
    lv_obj_t *number = lv_obj_get_child(row, 0);
    lv_obj_t *name = lv_obj_get_child(row, 1);

    lv_label_set_text_fmt(number, "%04u", (unsigned)(index + 1u));
    if(demo->cached[index]) {
        lv_label_set_text_fmt(name, "REC_20260920_%04u.wav", (unsigned)(index + 1u));
        lv_obj_set_style_bg_color(row,
                                  selected ? lv_color_hex(0x126782)
                                           : lv_color_hex(0x1B2735),
                                  0);
        return true;
    }
    lv_label_set_text(name, "Loading file metadata...");
    lv_obj_set_style_bg_color(row, selected ? lv_color_hex(0x5B4A22)
                                           : lv_color_hex(0x292B2E),
                              0);
    return false;
}

static bool demo_item_ready(void *user_ctx, uint32_t index)
{
    demo_t *demo = user_ctx;

    return index < DEMO_FILE_COUNT && demo->cached[index];
}

static void demo_finish_load(void *user_data)
{
    demo_t *demo = user_data;
    uint32_t index;

    demo->load_scheduled = false;
    if(demo->pending_generation != paged_list_generation(demo->list)) return;
    for(index = demo->pending_first; index <= demo->pending_last; ++index) {
        demo->cached[index] = true;
        if(index == DEMO_FILE_COUNT - 1u) break;
    }
    (void)paged_list_data_changed(
        demo->list,
        demo->pending_generation,
        demo->pending_first,
        demo->pending_last);
    demo_update_labels(demo);
}

static void demo_request_range(
    void *user_ctx, const paged_list_range_t *range)
{
    demo_t *demo = user_ctx;

    demo->pending_generation = range->generation;
    demo->pending_first = range->first_index;
    demo->pending_last = range->last_index;
    if(!demo->load_scheduled) {
        demo->load_scheduled = true;
        lv_async_call(demo_finish_load, demo);
    }
}

static void demo_on_event(
    void *user_ctx, const paged_list_event_t *event)
{
    demo_t *demo = user_ctx;

    demo_update_labels(demo);
    if(event->kind == PAGED_LIST_EVENT_ITEM_ACTIVATED) {
        lv_label_set_text_fmt(demo->status_label, "Opened file %04u",
                              (unsigned)(event->index + 1u));
    }
}

static bool demo_init(demo_t *demo)
{
    const paged_list_config_t config = {
        .item_count = DEMO_FILE_COUNT,
        .row_height = 48u,
        .row_gap = 7u,
        .visible_rows = DEMO_VISIBLE_ROWS,
        .prefetch_pages = 1u,
        .activate_on_click = true,
        .create_row = demo_create_row,
        .bind_row = demo_bind_row,
        .item_ready = demo_item_ready,
        .request_range = demo_request_range,
        .on_event = demo_on_event,
        .user_ctx = demo,
    };
    lv_obj_t *screen;
    lv_obj_t *title;
    lv_obj_t *hint;
    lv_obj_t *panel;
    lv_indev_t *mouse;
    lv_indev_t *keyboard;

    memset(demo, 0, sizeof(*demo));
    demo->display = lv_sdl_window_create(DEMO_WIDTH, DEMO_HEIGHT);
    if(demo->display == NULL) return false;
    lv_sdl_window_set_title(demo->display, "Paged file list demo");
    lv_display_set_default(demo->display);
    mouse = lv_sdl_mouse_create();
    keyboard = lv_sdl_keyboard_create();
    if(mouse == NULL || keyboard == NULL) return false;
    lv_indev_set_display(mouse, demo->display);
    lv_indev_set_display(keyboard, demo->display);

    screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0D141D), 0);
    title = lv_label_create(screen);
    lv_label_set_text(title, "Recording files");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF2F6FA), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 48, 30);
    hint = lv_label_create(screen);
    lv_label_set_text(hint, "Up/Down select   Left/Right page   Enter open");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8FA8BD), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -48, 34);
    panel = lv_obj_create(screen);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 704, 340);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 20);
    demo->page_label = lv_label_create(screen);
    lv_obj_set_style_text_color(demo->page_label, lv_color_hex(0x8FA8BD), 0);
    lv_obj_set_style_text_font(demo->page_label, &lv_font_montserrat_16, 0);
    lv_obj_align(demo->page_label, LV_ALIGN_BOTTOM_LEFT, 48, -24);
    demo->status_label = lv_label_create(screen);
    lv_obj_set_style_text_color(demo->status_label, lv_color_hex(0x8FE0B0), 0);
    lv_obj_set_style_text_font(demo->status_label, &lv_font_montserrat_16, 0);
    lv_obj_align(demo->status_label, LV_ALIGN_BOTTOM_RIGHT, -48, -24);
    demo->group = lv_group_create();
    if(demo->group == NULL) return false;
    lv_indev_set_group(keyboard, demo->group);
    {
        paged_list_config_t local_config = config;
        local_config.input_group = demo->group;
        demo->list = paged_list_create(panel, &local_config);
    }
    if(demo->list == NULL) return false;
    demo_update_labels(demo);
    return true;
}

int main(void)
{
    demo_t demo;

    lv_init();
    if(!demo_init(&demo)) {
        fprintf(stderr, "failed to initialize paged-list demo\n");
        return 1;
    }
    while(true) {
        (void)lv_timer_handler();
        SDL_Delay(5u);
    }
}
