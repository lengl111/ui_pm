#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <lvgl.h>

#include "home_pager/home_pager.h"

LV_FONT_DECLARE(SourceHanSansCN_Medium_28px_4bpp)
LV_FONT_DECLARE(SourceHanSansCN_Regular_24px_4bpp)

#define HOME_DEMO_WIDTH 800
#define HOME_DEMO_HEIGHT 480

typedef struct {
    lv_display_t *display;
    home_pager_t *pager;
    lv_obj_t *mock_page;
    uint32_t window_id;
    uint16_t selected_item;
    uint8_t selected_count;
    uint8_t page_changed_count;
} home_demo_t;

static lv_font_t home_demo_title_font;
static lv_font_t home_demo_fallback_font;

static const home_pager_item_t page_one_items[] = {
    {1u, "拍万物", "DISCOVER", LV_SYMBOL_EYE_OPEN, NULL,
     HOME_PAGER_ACCENT_YELLOW, false},
    {2u, "创意滤镜", "CREATE", LV_SYMBOL_IMAGE, NULL,
     HOME_PAGER_ACCENT_BLUE, false},
    {3u, "魔法变身", NULL, LV_SYMBOL_REFRESH, NULL,
     HOME_PAGER_ACCENT_PINK, false},
    {4u, "涂鸦生图", NULL, LV_SYMBOL_EDIT, NULL,
     HOME_PAGER_ACCENT_ORANGE, false},
};

static const home_pager_item_t page_two_items[] = {
    {5u, "拍物识字", "LEARN", LV_SYMBOL_EYE_OPEN, NULL,
     HOME_PAGER_ACCENT_GREEN, false},
    {6u, "拍汉字", NULL, LV_SYMBOL_EDIT, NULL,
     HOME_PAGER_ACCENT_YELLOW, false},
    {7u, "拍英文", NULL, LV_SYMBOL_FILE, NULL,
     HOME_PAGER_ACCENT_BLUE, false},
};

static const home_pager_item_t page_three_items[] = {
    {8u, "AI 对话", "CHAT", LV_SYMBOL_AUDIO, NULL,
     HOME_PAGER_ACCENT_PURPLE, false},
    {9u, "儿童故事", NULL, LV_SYMBOL_PLAY, NULL,
     HOME_PAGER_ACCENT_ORANGE, false},
    {10u, "儿歌点播", NULL, LV_SYMBOL_VOLUME_MAX, NULL,
     HOME_PAGER_ACCENT_PINK, false},
};

static const home_pager_item_t page_four_items[] = {
    {11u, "探索任务", "EXPLORE", LV_SYMBOL_OK, NULL,
     HOME_PAGER_ACCENT_YELLOW, false},
    {12u, "高清相机", "CAMERA", LV_SYMBOL_IMAGE, NULL,
     HOME_PAGER_ACCENT_BLUE, false},
    {13u, "设置", NULL, LV_SYMBOL_SETTINGS, NULL,
     HOME_PAGER_ACCENT_GREEN, false},
    {14u, "相册", NULL, LV_SYMBOL_DIRECTORY, NULL,
     HOME_PAGER_ACCENT_PURPLE, false},
};

static const home_pager_page_t home_pages[] = {
    {page_one_items, 4u},
    {page_two_items, 3u},
    {page_three_items, 3u},
    {page_four_items, 4u},
};

static const char *home_demo_item_title(uint16_t item_id)
{
    for(uint8_t page_index = 0u; page_index < 4u; ++page_index) {
        for(uint8_t item_index = 0u;
            item_index < home_pages[page_index].item_count;
            ++item_index) {
            const home_pager_item_t *item =
                &home_pages[page_index].items[item_index];

            if(item->id == item_id) return item->title;
        }
    }
    return "Home item";
}

static void home_demo_close_mock_page(home_demo_t *demo)
{
    if(demo->mock_page == NULL) return;
    lv_obj_delete(demo->mock_page);
    demo->mock_page = NULL;
}

static void home_demo_on_mock_back(lv_event_t *event)
{
    home_demo_close_mock_page(lv_event_get_user_data(event));
}

static void home_demo_show_mock_page(
    home_demo_t *demo, const home_pager_event_t *event)
{
    lv_obj_t *back_button;
    lv_obj_t *back_label;
    lv_obj_t *page_title;
    lv_obj_t *item_title;
    lv_obj_t *description;

    home_demo_close_mock_page(demo);
    demo->mock_page = lv_obj_create(lv_screen_active());
    if(demo->mock_page == NULL) return;
    lv_obj_remove_style_all(demo->mock_page);
    lv_obj_set_size(demo->mock_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(
        demo->mock_page, lv_color_hex(0x090B0F), 0);
    lv_obj_set_style_bg_opa(demo->mock_page, LV_OPA_COVER, 0);
    lv_obj_add_flag(demo->mock_page, LV_OBJ_FLAG_CLICKABLE);

    back_button = lv_button_create(demo->mock_page);
    lv_obj_set_size(back_button, 112, 48);
    lv_obj_align(back_button, LV_ALIGN_TOP_LEFT, 16, 12);
    lv_obj_set_style_radius(back_button, 10, 0);
    lv_obj_set_style_bg_color(back_button, lv_color_hex(0x2D333B), 0);
    lv_obj_add_event_cb(
        back_button, home_demo_on_mock_back, LV_EVENT_CLICKED, demo);
    back_label = lv_label_create(back_button);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Home");
    lv_obj_center(back_label);

    page_title = lv_label_create(demo->mock_page);
    lv_label_set_text(page_title, "模拟功能页");
    lv_obj_set_style_text_color(page_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(
        page_title, &home_demo_title_font, 0);
    lv_obj_align(page_title, LV_ALIGN_TOP_MID, 0, 28);

    item_title = lv_label_create(demo->mock_page);
    lv_label_set_text(item_title, home_demo_item_title(event->item_id));
    lv_obj_set_style_text_color(item_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(
        item_title, &home_demo_title_font, 0);
    lv_obj_align(item_title, LV_ALIGN_CENTER, 0, -28);

    description = lv_label_create(demo->mock_page);
    lv_label_set_text_fmt(
        description,
        "Item %u\n这是“%s”的模拟目标页面",
        (unsigned)event->item_id,
        home_demo_item_title(event->item_id));
    lv_obj_set_style_text_align(description, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(description, lv_color_hex(0xB8C1CC), 0);
    lv_obj_align(description, LV_ALIGN_CENTER, 0, 48);
    lv_obj_move_foreground(demo->mock_page);
}

static void home_demo_on_event(
    void *user_ctx, const home_pager_event_t *event)
{
    home_demo_t *demo = user_ctx;

    if(event->kind == HOME_PAGER_EVENT_ITEM_SELECTED) {
        demo->selected_item = event->item_id;
        ++demo->selected_count;
        printf(
            "selected item %u on page %u\n",
            (unsigned)event->item_id,
            (unsigned)event->page_index + 1u);
        home_demo_show_mock_page(demo, event);
    }
    else {
        ++demo->page_changed_count;
        printf("page changed to %u\n", (unsigned)event->page_index + 1u);
    }
}

static bool home_demo_init_display(home_demo_t *demo)
{
    SDL_Renderer *renderer;
    SDL_Window *window;
    lv_group_t *group;
    lv_indev_t *mouse;
    lv_indev_t *mousewheel;
    lv_indev_t *keyboard;

    memset(demo, 0, sizeof(*demo));
    demo->display = lv_sdl_window_create(HOME_DEMO_WIDTH, HOME_DEMO_HEIGHT);
    if(demo->display == NULL) return false;
    lv_sdl_window_set_title(demo->display, "Home pager layout");
    lv_display_set_default(demo->display);
    renderer = lv_sdl_window_get_renderer(demo->display);
    window = renderer != NULL ? SDL_RenderGetWindow(renderer) : NULL;
    if(window == NULL) return false;
    demo->window_id = SDL_GetWindowID(window);

    group = lv_group_create();
    mouse = lv_sdl_mouse_create();
    mousewheel = lv_sdl_mousewheel_create();
    keyboard = lv_sdl_keyboard_create();
    if(group == NULL || mouse == NULL || mousewheel == NULL ||
       keyboard == NULL) {
        return false;
    }
    lv_group_set_default(group);
    lv_indev_set_display(mouse, demo->display);
    lv_indev_set_display(mousewheel, demo->display);
    lv_indev_set_display(keyboard, demo->display);
    lv_indev_set_group(mousewheel, group);
    lv_indev_set_group(keyboard, group);
    return true;
}

static void home_demo_create_status_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_t *time = lv_label_create(bar);
    lv_obj_t *status = lv_label_create(bar);

    lv_obj_remove_style_all(bar);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, LV_PCT(100), 60);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x232323), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    lv_label_set_text(time, "9月5日 12:00");
    lv_obj_set_style_text_color(time, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(
        time, &SourceHanSansCN_Medium_28px_4bpp, 0);
    lv_obj_align(time, LV_ALIGN_LEFT_MID, 20, 0);

    lv_label_set_text(
        status, LV_SYMBOL_WIFI "  " LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_color(status, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_32, 0);
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_move_foreground(bar);
}

static bool home_demo_create(home_demo_t *demo)
{
    home_demo_title_font = SourceHanSansCN_Medium_28px_4bpp;
    home_demo_fallback_font = SourceHanSansCN_Regular_24px_4bpp;
    home_demo_title_font.fallback = &home_demo_fallback_font;
    home_demo_fallback_font.fallback = LV_FONT_DEFAULT;

    const home_pager_config_t config = {
        .pages = home_pages,
        .page_count = 4u,
        .initial_page = 0u,
        .lazy_load = true,
        .top_inset = 60u,
        .symbol_font = &lv_font_montserrat_32,
        .title_font = &home_demo_title_font,
        .caption_font = LV_FONT_DEFAULT,
        .on_event = home_demo_on_event,
        .user_ctx = demo,
    };
    lv_obj_t *screen = lv_screen_active();

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x090B0F), 0);
    demo->pager = home_pager_create(screen, &config);
    if(demo->pager == NULL) return false;
    home_demo_create_status_bar(screen);
    return true;
}

static void home_demo_pump(home_demo_t *demo, uint32_t duration_ms)
{
    uint32_t start = lv_tick_get();

    do {
        (void)lv_timer_handler();
        lv_refr_now(demo->display);
        SDL_Delay(2u);
    } while(lv_tick_elaps(start) < duration_ms);
}

static bool home_demo_push_mouse_button(
    home_demo_t *demo, uint32_t type, int32_t x, int32_t y)
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

static bool home_demo_push_mouse_motion(
    home_demo_t *demo,
    int32_t x,
    int32_t y,
    int32_t relative_x)
{
    SDL_Event event;

    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEMOTION;
    event.motion.windowID = demo->window_id;
    event.motion.state = SDL_BUTTON_LMASK;
    event.motion.x = x;
    event.motion.y = y;
    event.motion.xrel = relative_x;
    return SDL_PushEvent(&event) == 1;
}

static bool home_demo_drag(
    home_demo_t *demo,
    int32_t start_x,
    int32_t end_x,
    uint32_t steps,
    uint32_t step_delay_ms,
    uint32_t settle_ms)
{
    int32_t previous_x = start_x;

    if(steps == 0u) return false;
    if(!home_demo_push_mouse_button(
           demo, SDL_MOUSEBUTTONDOWN, start_x, 240)) {
        return false;
    }
    home_demo_pump(demo, 20u);
    for(uint32_t step = 1u; step <= steps; ++step) {
        int32_t x = start_x +
                    (end_x - start_x) * (int32_t)step / (int32_t)steps;

        if(!home_demo_push_mouse_motion(
               demo, x, 240, x - previous_x)) {
            return false;
        }
        home_demo_pump(demo, step_delay_ms);
        previous_x = x;
    }
    if(!home_demo_push_mouse_button(
           demo, SDL_MOUSEBUTTONUP, end_x, 240)) {
        return false;
    }
    home_demo_pump(demo, settle_ms);
    return true;
}

static bool home_demo_probe_indicator_transition(home_demo_t *demo)
{
    lv_obj_t *root = home_pager_root(demo->pager);
    lv_obj_t *indicator_container;
    lv_obj_t *track;
    lv_obj_t *current_indicator;
    lv_obj_t *next_indicator;
    int32_t track_width;

    if(root == NULL) return false;
    indicator_container = lv_obj_get_child(root, 1);
    if(indicator_container == NULL) return false;
    track = lv_obj_get_child(indicator_container, 0);
    if(track == NULL) return false;
    current_indicator = lv_obj_get_child(track, 0);
    next_indicator = lv_obj_get_child(track, 1);
    if(current_indicator == NULL || next_indicator == NULL) return false;
    track_width = lv_obj_get_width(track);
    if(!home_demo_push_mouse_button(
           demo, SDL_MOUSEBUTTONDOWN, 650, 240)) {
        return false;
    }
    home_demo_pump(demo, 20u);
    if(!home_demo_push_mouse_motion(demo, 250, 240, -400)) return false;
    home_demo_pump(demo, 2u);
    lv_obj_update_layout(root);
    if(lv_obj_get_width(track) != track_width ||
       lv_obj_get_width(current_indicator) >= 24 ||
       lv_obj_get_width(next_indicator) <= 8) {
        fprintf(
            stderr,
            "indicator probe failed: track=%d/%d current=%d next=%d\n",
            (int)lv_obj_get_width(track),
            (int)track_width,
            (int)lv_obj_get_width(current_indicator),
            (int)lv_obj_get_width(next_indicator));
        return false;
    }
    if(!home_demo_push_mouse_button(
           demo, SDL_MOUSEBUTTONUP, 250, 240)) {
        return false;
    }
    home_demo_pump(demo, 650u);
    return true;
}

static bool home_demo_expect_page(
    home_demo_t *demo,
    const char *stage,
    uint8_t expected_page,
    uint8_t expected_events)
{
    uint8_t actual_page = home_pager_get_page(demo->pager);

    if(actual_page != expected_page ||
       demo->page_changed_count != expected_events) {
        fprintf(
            stderr,
            "smoke failed at %s: page=%u events=%u expected_page=%u "
            "expected_events=%u\n",
            stage,
            (unsigned)actual_page,
            (unsigned)demo->page_changed_count,
            (unsigned)expected_page,
            (unsigned)expected_events);
        return false;
    }
    return true;
}

static bool home_demo_expect_snap(
    home_demo_t *demo,
    const char *stage,
    uint8_t page_index,
    int32_t expected_width)
{
    lv_obj_t *root = home_pager_root(demo->pager);
    lv_obj_t *carousel;
    lv_obj_t *page;
    int32_t actual_width;
    int32_t actual_scroll;
    int32_t expected_scroll;

    if(root == NULL) return false;
    carousel = lv_obj_get_child(root, 0);
    if(carousel == NULL || page_index >= lv_obj_get_child_count(carousel)) {
        return false;
    }
    lv_obj_update_layout(root);
    page = lv_obj_get_child(carousel, page_index + 1u);
    actual_width = lv_obj_get_width(carousel);
    actual_scroll = lv_obj_get_scroll_x(carousel);
    expected_scroll = lv_obj_get_x(page);
    if(actual_width != expected_width || actual_scroll != expected_scroll) {
        fprintf(
            stderr,
            "smoke failed at %s: width=%d/%d scroll=%d/%d\n",
            stage,
            (int)actual_width,
            (int)expected_width,
            (int)actual_scroll,
            (int)expected_scroll);
        return false;
    }
    return true;
}

static bool home_demo_swipe_left(home_demo_t *demo)
{
    return home_demo_drag(demo, 650, 150, 4u, 15u, 650u);
}

static void write_u16_le(FILE *file, uint16_t value)
{
    const uint8_t bytes[2] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
    };
    (void)fwrite(bytes, sizeof(bytes), 1u, file);
}

static void write_u32_le(FILE *file, uint32_t value)
{
    const uint8_t bytes[4] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 24),
    };
    (void)fwrite(bytes, sizeof(bytes), 1u, file);
}

static bool home_demo_write_snapshot(home_demo_t *demo, const char *path)
{
    lv_draw_buf_t *snapshot;
    FILE *file;
    uint32_t row_size;
    uint32_t image_size;

    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(demo->display);
    snapshot = lv_snapshot_take(
        lv_screen_active(), LV_COLOR_FORMAT_RGB888);
    if(snapshot == NULL) return false;
    file = fopen(path, "wb");
    if(file == NULL) {
        lv_draw_buf_destroy(snapshot);
        return false;
    }

    row_size = (snapshot->header.w * 3u + 3u) & ~UINT32_C(3);
    image_size = row_size * snapshot->header.h;
    (void)fwrite("BM", 2u, 1u, file);
    write_u32_le(file, UINT32_C(54) + image_size);
    write_u16_le(file, 0u);
    write_u16_le(file, 0u);
    write_u32_le(file, 54u);
    write_u32_le(file, 40u);
    write_u32_le(file, snapshot->header.w);
    write_u32_le(file, snapshot->header.h);
    write_u16_le(file, 1u);
    write_u16_le(file, 24u);
    write_u32_le(file, 0u);
    write_u32_le(file, image_size);
    write_u32_le(file, 2835u);
    write_u32_le(file, 2835u);
    write_u32_le(file, 0u);
    write_u32_le(file, 0u);

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

static int home_demo_run_smoke(home_demo_t *demo)
{
    lv_obj_t *carousel;
    lv_obj_t *first_page;
    lv_obj_t *first_card;

    if(home_pager_get_created_page_count(demo->pager) != 1u) return 1;
    if(!home_demo_probe_indicator_transition(demo)) {
        fprintf(stderr, "smoke failed at indicator transition\n");
        return 1;
    }
    if(!home_demo_expect_page(demo, "indicator settle", 1u, 1u)) return 1;
    home_pager_set_page(demo->pager, 0u, LV_ANIM_OFF);
    if(!home_demo_expect_page(demo, "return to page zero", 0u, 2u)) {
        return 1;
    }
    if(!home_demo_drag(demo, 650, 500, 75u, 4u, 650u)) return 1;
    if(!home_demo_expect_page(demo, "below threshold", 0u, 2u)) return 1;
    if(!home_demo_drag(demo, 650, 450, 100u, 4u, 650u)) return 1;
    if(!home_demo_expect_page(demo, "above threshold", 1u, 3u)) return 1;
    home_pager_set_page(demo->pager, 0u, LV_ANIM_OFF);
    if(!home_demo_drag(demo, 650, 626, 1u, 1u, 650u)) return 1;
    if(!home_demo_expect_page(demo, "short drag", 1u, 5u)) return 1;
    home_pager_set_page(demo->pager, 0u, LV_ANIM_OFF);
    if(!home_demo_drag(demo, 400, 430, 1u, 1u, 650u)) return 1;
    if(!home_demo_expect_page(demo, "first page wraps to last", 3u, 7u)) {
        return 1;
    }
    if(!home_demo_swipe_left(demo)) return 1;
    if(home_pager_get_page(demo->pager) != 0u ||
       demo->page_changed_count != 8u ||
       home_pager_get_created_page_count(demo->pager) < 2u) {
        return 1;
    }
    home_pager_set_page(demo->pager, 0u, LV_ANIM_OFF);

    carousel = lv_obj_get_child(home_pager_root(demo->pager), 0);
    first_page = lv_obj_get_child(carousel, 1);
    first_card = lv_obj_get_child(first_page, 0);
    if(first_card == NULL) return 1;
    if(!home_demo_push_mouse_button(
           demo, SDL_MOUSEBUTTONDOWN, 156, 214)) {
        return 1;
    }
    home_demo_pump(demo, 20u);
    if(!home_demo_push_mouse_button(
           demo, SDL_MOUSEBUTTONUP, 156, 214)) {
        return 1;
    }
    home_demo_pump(demo, 80u);
    if(demo->selected_count != 1u || demo->selected_item != 1u) return 1;
    if(demo->mock_page == NULL) return 1;
    if(!home_demo_push_mouse_button(
           demo, SDL_MOUSEBUTTONDOWN, 60, 36)) {
        return 1;
    }
    home_demo_pump(demo, 20u);
    if(!home_demo_push_mouse_button(
           demo, SDL_MOUSEBUTTONUP, 60, 36)) {
        return 1;
    }
    home_demo_pump(demo, 50u);
    if(demo->mock_page != NULL) return 1;

    for(uint8_t page_index = 0u; page_index < 4u; ++page_index) {
        home_pager_set_page(demo->pager, page_index, LV_ANIM_OFF);
        home_demo_pump(demo, 15u);
        if(home_pager_get_page(demo->pager) != page_index) return 1;
    }
    if(home_pager_get_created_page_count(demo->pager) != 4u) return 1;
    if(demo->page_changed_count != 11u) return 1;

    home_pager_set_page(demo->pager, 1u, LV_ANIM_ON);
    home_pager_set_page(demo->pager, 2u, LV_ANIM_ON);
    home_demo_pump(demo, 650u);
    if(!home_demo_expect_page(demo, "replace page animation", 2u, 12u)) {
        return 1;
    }
    if(!home_demo_expect_snap(demo, "page two snap", 2u, 800)) return 1;

    lv_obj_set_width(home_pager_root(demo->pager), 640);
    home_demo_pump(demo, 30u);
    if(!home_demo_expect_page(demo, "resize narrow", 2u, 12u) ||
       !home_demo_expect_snap(demo, "resize narrow snap", 2u, 640)) {
        return 1;
    }
    lv_obj_set_width(home_pager_root(demo->pager), LV_PCT(100));
    home_demo_pump(demo, 30u);
    if(!home_demo_expect_snap(demo, "resize restore snap", 2u, 800)) {
        return 1;
    }

    home_pager_set_page(demo->pager, 3u, LV_ANIM_ON);
    home_demo_pump(demo, 50u);
    lv_obj_set_width(home_pager_root(demo->pager), 640);
    home_demo_pump(demo, 1200u);
    if(!home_demo_expect_page(demo, "resize during animation", 3u, 13u) ||
       !home_demo_expect_snap(
           demo, "resize during animation snap", 3u, 640)) {
        return 1;
    }
    lv_obj_set_width(home_pager_root(demo->pager), LV_PCT(100));
    home_demo_pump(demo, 30u);
    if(!home_demo_expect_snap(
           demo, "resize after animation snap", 3u, 800)) {
        return 1;
    }

    home_pager_set_page(demo->pager, 0u, LV_ANIM_OFF);
    if(!home_demo_drag(demo, 400, 430, 1u, 1u, 650u) ||
        !home_demo_expect_page(demo, "first page wraps right", 3u, 15u)) {
        return 1;
    }
    home_pager_set_page(demo->pager, 3u, LV_ANIM_OFF);
    if(!home_demo_drag(demo, 400, 370, 1u, 1u, 650u) ||
       !home_demo_expect_page(demo, "last page wraps left", 0u, 16u)) {
        return 1;
    }
    printf("home pager smoke passed\n");
    return 0;
}

static int home_demo_write_screenshots(
    home_demo_t *demo, const char *directory)
{
    char path[512];

    for(uint8_t page_index = 0u; page_index < 4u; ++page_index) {
        int written;

        home_pager_set_page(demo->pager, page_index, LV_ANIM_OFF);
        home_demo_pump(demo, 15u);
        written = snprintf(
            path,
            sizeof(path),
            "%s/home-page-%u.bmp",
            directory,
            (unsigned)page_index + 1u);
        if(written < 0 || (size_t)written >= sizeof(path) ||
           !home_demo_write_snapshot(demo, path)) {
            return 1;
        }
    }
    printf("home pager screenshots written to %s\n", directory);
    return 0;
}

int main(int argc, char **argv)
{
    bool run_smoke = argc == 2 && strcmp(argv[1], "--smoke") == 0;
    bool write_screenshots =
        argc == 3 && strcmp(argv[1], "--screenshots") == 0;
    home_demo_t demo;

    if(argc > 3 || (argc == 2 && !run_smoke) ||
       (argc == 3 && !write_screenshots)) {
        fprintf(
            stderr,
            "usage: home_pager_demo [--smoke | --screenshots DIR]\n");
        return 2;
    }

    lv_init();
    if(!home_demo_init_display(&demo) || !home_demo_create(&demo)) {
        fprintf(stderr, "failed to initialize Home pager demo\n");
        return 1;
    }
    home_demo_pump(&demo, 20u);

    if(run_smoke) return home_demo_run_smoke(&demo);
    if(write_screenshots) return home_demo_write_screenshots(&demo, argv[2]);

    while(true) {
        (void)lv_timer_handler();
        SDL_Delay(5u);
    }
}
