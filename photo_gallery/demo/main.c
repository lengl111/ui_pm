#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <lvgl.h>

#include "photo_gallery/photo_gallery.h"

#define PHOTO_DEMO_WIDTH 800
#define PHOTO_DEMO_HEIGHT 480
#define PHOTO_DEMO_ITEM_COUNT 23u
#define PHOTO_DEMO_IMAGE_WIDTH 160u
#define PHOTO_DEMO_IMAGE_HEIGHT 100u

typedef struct {
    lv_display_t *display;
    photo_gallery_t *gallery;
    uint32_t window_id;
    uint32_t clicked_count;
    uint32_t page_changed_count;
    uint32_t viewer_changed_count;
    uint32_t range_request_count;
    uint32_t last_event_id;
} photo_demo_t;

static uint32_t photo_demo_pixels[PHOTO_DEMO_ITEM_COUNT]
                                   [PHOTO_DEMO_IMAGE_WIDTH *
                                    PHOTO_DEMO_IMAGE_HEIGHT];
static lv_image_dsc_t photo_demo_images[PHOTO_DEMO_ITEM_COUNT];

static uint32_t photo_demo_color(uint32_t index)
{
    static const uint32_t colors[] = {
        0xEF6A6A, 0xE8954A, 0xE6C84F, 0x7BCB73,
        0x4FB7A5, 0x50A7D9, 0x6579D8, 0x9876D8,
        0xD06BB7, 0xD95C8A, 0x75828E, 0x4C9EAF,
    };
    return colors[index % (sizeof(colors) / sizeof(colors[0]))];
}

static void photo_demo_make_images(void)
{
    for(uint32_t index = 0u; index < PHOTO_DEMO_ITEM_COUNT; ++index) {
        uint32_t color = photo_demo_color(index);
        uint32_t r = (color >> 16) & 0xFFu;
        uint32_t g = (color >> 8) & 0xFFu;
        uint32_t b = color & 0xFFu;

        for(uint32_t y = 0u; y < PHOTO_DEMO_IMAGE_HEIGHT; ++y) {
            for(uint32_t x = 0u; x < PHOTO_DEMO_IMAGE_WIDTH; ++x) {
                uint32_t shade = (x * 28u + y * 18u + index * 11u) % 42u;
                uint32_t rr = r > shade ? r - shade : 0u;
                uint32_t gg = g > shade ? g - shade : 0u;
                uint32_t bb = b > shade ? b - shade : 0u;
                photo_demo_pixels[index][y * PHOTO_DEMO_IMAGE_WIDTH + x] =
                    0xFF000000u | (rr << 16) | (gg << 8) | bb;
            }
        }
        photo_demo_images[index].header.magic = LV_IMAGE_HEADER_MAGIC;
        photo_demo_images[index].header.cf = LV_COLOR_FORMAT_ARGB8888;
        photo_demo_images[index].header.w = PHOTO_DEMO_IMAGE_WIDTH;
        photo_demo_images[index].header.h = PHOTO_DEMO_IMAGE_HEIGHT;
        photo_demo_images[index].header.stride = PHOTO_DEMO_IMAGE_WIDTH * 4u;
        photo_demo_images[index].data_size = sizeof(photo_demo_pixels[index]);
        photo_demo_images[index].data = (const uint8_t *)photo_demo_pixels[index];
    }
}

static bool photo_demo_item_provider(
    void *user_ctx, uint32_t index, photo_gallery_item_t *item)
{
    (void)user_ctx;
    if(item == NULL || index >= PHOTO_DEMO_ITEM_COUNT) return false;
    item->id = 1000u + index;
    item->thumbnail_src = &photo_demo_images[index];
    item->image_src = &photo_demo_images[index];
    item->title = index % 5u == 0u ? "HD sample" : "Photo sample";
    item->disabled = false;
    return true;
}

static void photo_demo_request_range(
    void *user_ctx, uint32_t from, uint32_t to)
{
    photo_demo_t *demo = user_ctx;

    ++demo->range_request_count;
    printf("range requested: %u..%u\n", (unsigned)from, (unsigned)to);
}

static void photo_demo_on_event(
    void *user_ctx, const photo_gallery_event_t *event)
{
    photo_demo_t *demo = user_ctx;

    demo->last_event_id = event->id;
    switch(event->kind) {
        case PHOTO_GALLERY_EVENT_ITEM_CLICKED:
            ++demo->clicked_count;
            printf(
                "item clicked: index=%u id=%u\n",
                (unsigned)event->index,
                (unsigned)event->id);
            photo_gallery_show_viewer(demo->gallery, event->index, LV_ANIM_OFF);
            break;
        case PHOTO_GALLERY_EVENT_SELECTION_CHANGED:
            printf(
                "selection: index=%u selected=%d total=%u\n",
                (unsigned)event->index,
                event->selected ? 1 : 0,
                (unsigned)event->selected_count);
            break;
        case PHOTO_GALLERY_EVENT_PAGE_CHANGED:
            ++demo->page_changed_count;
            printf(
                "page changed: %u/%u\n",
                (unsigned)(event->page_index + 1u),
                (unsigned)event->page_count);
            break;
        case PHOTO_GALLERY_EVENT_VIEWER_OPENED:
            printf("viewer opened: %u\n", (unsigned)event->index);
            break;
        case PHOTO_GALLERY_EVENT_VIEWER_INDEX_CHANGED:
            ++demo->viewer_changed_count;
            printf("viewer index: %u\n", (unsigned)event->index);
            break;
        case PHOTO_GALLERY_EVENT_VIEWER_CLOSED:
            printf("viewer closed\n");
            break;
        case PHOTO_GALLERY_EVENT_VIEWER_EDGE_REACHED:
            printf("viewer edge reached\n");
            break;
        default:
            break;
    }
}

static void photo_demo_on_key(lv_event_t *event)
{
    photo_demo_t *demo = lv_event_get_user_data(event);

    if(lv_event_get_key(event) == LV_KEY_ESC &&
       photo_gallery_viewer_is_open(demo->gallery)) {
        photo_gallery_close_viewer(demo->gallery, LV_ANIM_OFF);
    }
}

static bool photo_demo_init(photo_demo_t *demo)
{
    SDL_Renderer *renderer;
    SDL_Window *window;
    lv_indev_t *mouse;
    lv_indev_t *mousewheel;
    lv_indev_t *keyboard;
    const photo_gallery_config_t config = {
        .item_count = PHOTO_DEMO_ITEM_COUNT,
        .item_provider = photo_demo_item_provider,
        .request_range = photo_demo_request_range,
        .columns = 3u,
        .rows_per_page = 2u,
        .cache_size = 12u,
        .prefetch_items = 3u,
        .thumbnail_width = 230u,
        .thumbnail_height = 140u,
        .gap_x = 14u,
        .gap_y = 12u,
        .padding_left = 18u,
        .padding_right = 18u,
        .padding_top = 70u,
        .padding_bottom = 52u,
        .viewer_swipe_threshold = 64u,
        .title = "Photo library",
        .snap_to_page = true,
        .selection_mode = false,
        .title_font = &lv_font_montserrat_18,
        .body_font = &lv_font_montserrat_18,
        .on_event = photo_demo_on_event,
        .user_ctx = demo,
    };

    memset(demo, 0, sizeof(*demo));
    demo->display = lv_sdl_window_create(PHOTO_DEMO_WIDTH, PHOTO_DEMO_HEIGHT);
    if(demo->display == NULL) return false;
    lv_sdl_window_set_title(demo->display, "Photo gallery");
    lv_display_set_default(demo->display);
    renderer = lv_sdl_window_get_renderer(demo->display);
    window = renderer != NULL ? SDL_GetWindowFromID(
                                    SDL_GetWindowID(SDL_RenderGetWindow(renderer)))
                              : NULL;
    if(window == NULL) return false;
    demo->window_id = SDL_GetWindowID(window);

    mouse = lv_sdl_mouse_create();
    mousewheel = lv_sdl_mousewheel_create();
    keyboard = lv_sdl_keyboard_create();
    if(mouse == NULL || mousewheel == NULL || keyboard == NULL) return false;
    lv_indev_set_display(mouse, demo->display);
    lv_indev_set_display(mousewheel, demo->display);
    lv_indev_set_display(keyboard, demo->display);

    demo->gallery = photo_gallery_create(lv_screen_active(), &config);
    if(demo->gallery == NULL) return false;
    lv_obj_add_event_cb(
        photo_gallery_root(demo->gallery), photo_demo_on_key, LV_EVENT_KEY, demo);
    return true;
}

static void photo_demo_pump(photo_demo_t *demo, uint32_t duration_ms)
{
    uint32_t start = lv_tick_get();

    (void)demo;

    while(lv_tick_elaps(start) < duration_ms) {
        (void)lv_timer_handler();
        SDL_Delay(2u);
    }
}

static void photo_demo_write_u16(FILE *file, uint16_t value)
{
    const uint8_t bytes[2] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
    };
    (void)fwrite(bytes, sizeof(bytes), 1u, file);
}

static void photo_demo_write_u32(FILE *file, uint32_t value)
{
    const uint8_t bytes[4] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 24),
    };
    (void)fwrite(bytes, sizeof(bytes), 1u, file);
}

static bool photo_demo_write_snapshot(photo_demo_t *demo, const char *path)
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
    photo_demo_write_u32(file, 54u + image_size);
    photo_demo_write_u16(file, 0u);
    photo_demo_write_u16(file, 0u);
    photo_demo_write_u32(file, 54u);
    photo_demo_write_u32(file, 40u);
    photo_demo_write_u32(file, snapshot->header.w);
    photo_demo_write_u32(file, snapshot->header.h);
    photo_demo_write_u16(file, 1u);
    photo_demo_write_u16(file, 24u);
    photo_demo_write_u32(file, 0u);
    photo_demo_write_u32(file, image_size);
    photo_demo_write_u32(file, 2835u);
    photo_demo_write_u32(file, 2835u);
    photo_demo_write_u32(file, 0u);
    photo_demo_write_u32(file, 0u);
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

static bool photo_demo_click(photo_demo_t *demo, int32_t x, int32_t y)
{
    SDL_Event event;

    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.windowID = demo->window_id;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.state = SDL_PRESSED;
    event.button.x = x;
    event.button.y = y;
    if(SDL_PushEvent(&event) != 1) return false;
    photo_demo_pump(demo, 20u);
    event.type = SDL_MOUSEBUTTONUP;
    event.button.state = SDL_RELEASED;
    if(SDL_PushEvent(&event) != 1) return false;
    photo_demo_pump(demo, 50u);
    return true;
}

static int photo_demo_smoke(photo_demo_t *demo)
{
    if(photo_gallery_page_count(demo->gallery) != 4u) return 1;
    photo_gallery_set_selection_mode(demo->gallery, true);
    if(!photo_demo_click(demo, 140, 155)) return 1;
    if(photo_gallery_selected_count(demo->gallery) != 1u) return 1;
    photo_gallery_set_page(demo->gallery, 3u, LV_ANIM_OFF);
    if(photo_gallery_current_page(demo->gallery) != 3u) return 1;
    photo_gallery_set_item_count(demo->gallery, 8u);
    if(photo_gallery_page_count(demo->gallery) != 2u ||
       photo_gallery_current_page(demo->gallery) != 1u ||
       !photo_gallery_item_selected(demo->gallery, 0u) ||
       photo_gallery_selected_count(demo->gallery) != 1u) {
        return 1;
    }
    photo_gallery_set_item_count(demo->gallery, PHOTO_DEMO_ITEM_COUNT);
    if(!photo_gallery_item_selected(demo->gallery, 0u) ||
       photo_gallery_selected_count(demo->gallery) != 1u) {
        return 1;
    }
    photo_gallery_set_selection_mode(demo->gallery, false);
    if(!photo_gallery_show_viewer(demo->gallery, 4u, LV_ANIM_OFF)) return 1;
    if(demo->last_event_id != 1004u) return 1;
    if(!photo_gallery_viewer_set_index(demo->gallery, 5u, LV_ANIM_OFF)) return 1;
    if(photo_gallery_viewer_index(demo->gallery) != 5u) return 1;
    if(demo->last_event_id != 1005u) return 1;
    photo_gallery_close_viewer(demo->gallery, LV_ANIM_OFF);
    if(photo_gallery_viewer_is_open(demo->gallery)) return 1;
    printf("photo gallery smoke passed\n");
    return 0;
}

int main(int argc, char **argv)
{
    bool run_smoke = argc == 2 && strcmp(argv[1], "--smoke") == 0;
    bool write_screenshot =
        argc == 3 && strcmp(argv[1], "--screenshot") == 0;
    photo_demo_t demo;

    if(argc > 3 || (argc == 2 && !run_smoke) ||
       (argc == 3 && !write_screenshot)) {
        fprintf(
            stderr,
            "usage: photo_gallery_demo [--smoke | --screenshot FILE]\n");
        return 2;
    }
    lv_init();
    photo_demo_make_images();
    if(!photo_demo_init(&demo)) {
        fprintf(stderr, "failed to initialize photo gallery demo\n");
        return 1;
    }
    photo_demo_pump(&demo, 100u);
    if(run_smoke) return photo_demo_smoke(&demo);
    if(write_screenshot) {
        return photo_demo_write_snapshot(&demo, argv[2]) ? 0 : 1;
    }
    while(true) {
        (void)lv_timer_handler();
        SDL_Delay(5u);
    }
}
