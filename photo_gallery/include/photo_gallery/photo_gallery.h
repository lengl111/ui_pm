#ifndef PHOTO_GALLERY_PHOTO_GALLERY_H
#define PHOTO_GALLERY_PHOTO_GALLERY_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PHOTO_GALLERY_MAX_CACHE_ITEMS 32u

typedef struct photo_gallery photo_gallery_t;

typedef struct {
    uint32_t id;
    const void *thumbnail_src;
    const void *image_src;
    const char *title;
    bool disabled;
} photo_gallery_item_t;

typedef bool (*photo_gallery_item_provider_fn)(
    void *user_ctx, uint32_t index, photo_gallery_item_t *item);
typedef void (*photo_gallery_range_request_fn)(
    void *user_ctx, uint32_t from, uint32_t to);

typedef enum {
    PHOTO_GALLERY_EVENT_ITEM_CLICKED = 0,
    PHOTO_GALLERY_EVENT_SELECTION_CHANGED,
    PHOTO_GALLERY_EVENT_PAGE_CHANGED,
    PHOTO_GALLERY_EVENT_VIEWER_OPENED,
    PHOTO_GALLERY_EVENT_VIEWER_INDEX_CHANGED,
    PHOTO_GALLERY_EVENT_VIEWER_CLOSED,
    PHOTO_GALLERY_EVENT_VIEWER_EDGE_REACHED,
} photo_gallery_event_kind_t;

typedef struct {
    photo_gallery_event_kind_t kind;
    uint32_t index;
    uint32_t id;
    uint32_t page_index;
    uint32_t page_count;
    uint32_t selected_count;
    bool selected;
    lv_dir_t direction;
} photo_gallery_event_t;

typedef void (*photo_gallery_event_fn)(
    void *user_ctx, const photo_gallery_event_t *event);

typedef struct {
    const photo_gallery_item_t *items;
    uint32_t item_count;
    photo_gallery_item_provider_fn item_provider;
    photo_gallery_range_request_fn request_range;
    uint8_t columns;
    uint8_t rows_per_page;
    uint8_t cache_size;
    uint16_t prefetch_items;
    uint16_t thumbnail_width;
    uint16_t thumbnail_height;
    uint16_t gap_x;
    uint16_t gap_y;
    uint16_t padding_left;
    uint16_t padding_right;
    uint16_t padding_top;
    uint16_t padding_bottom;
    uint16_t viewer_swipe_threshold;
    const char *title;
    bool snap_to_page;
    bool selection_mode;
    const lv_font_t *title_font;
    const lv_font_t *body_font;
    photo_gallery_event_fn on_event;
    void *user_ctx;
} photo_gallery_config_t;

photo_gallery_t *photo_gallery_create(
    lv_obj_t *parent, const photo_gallery_config_t *config);
void photo_gallery_destroy(photo_gallery_t *gallery);

lv_obj_t *photo_gallery_root(photo_gallery_t *gallery);
lv_obj_t *photo_gallery_list(photo_gallery_t *gallery);
lv_obj_t *photo_gallery_viewer_root(photo_gallery_t *gallery);

void photo_gallery_set_item_count(
    photo_gallery_t *gallery, uint32_t item_count);
void photo_gallery_data_changed(
    photo_gallery_t *gallery, uint32_t from, uint32_t to);
uint32_t photo_gallery_item_count(const photo_gallery_t *gallery);

void photo_gallery_set_page(
    photo_gallery_t *gallery, uint32_t page_index, lv_anim_enable_t animation);
uint32_t photo_gallery_current_page(const photo_gallery_t *gallery);
uint32_t photo_gallery_page_count(const photo_gallery_t *gallery);
void photo_gallery_refresh(photo_gallery_t *gallery);

void photo_gallery_set_selection_mode(
    photo_gallery_t *gallery, bool enabled);
bool photo_gallery_selection_mode(const photo_gallery_t *gallery);
void photo_gallery_set_item_selected(
    photo_gallery_t *gallery, uint32_t index, bool selected);
bool photo_gallery_item_selected(
    const photo_gallery_t *gallery, uint32_t index);
uint32_t photo_gallery_selected_count(const photo_gallery_t *gallery);
void photo_gallery_clear_selection(photo_gallery_t *gallery);

bool photo_gallery_show_viewer(
    photo_gallery_t *gallery,
    uint32_t index,
    lv_anim_enable_t animation);
void photo_gallery_close_viewer(
    photo_gallery_t *gallery, lv_anim_enable_t animation);
bool photo_gallery_viewer_is_open(const photo_gallery_t *gallery);
uint32_t photo_gallery_viewer_index(const photo_gallery_t *gallery);
bool photo_gallery_viewer_set_index(
    photo_gallery_t *gallery,
    uint32_t index,
    lv_anim_enable_t animation);

#ifdef __cplusplus
}
#endif

#endif
