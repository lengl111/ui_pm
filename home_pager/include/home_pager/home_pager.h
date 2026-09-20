#ifndef HOME_PAGER_HOME_PAGER_H
#define HOME_PAGER_HOME_PAGER_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOME_PAGER_MAX_PAGES 8u
#define HOME_PAGER_MAX_ITEMS_PER_PAGE 4u

typedef struct home_pager home_pager_t;

typedef enum {
    HOME_PAGER_ACCENT_YELLOW = 0,
    HOME_PAGER_ACCENT_BLUE,
    HOME_PAGER_ACCENT_PINK,
    HOME_PAGER_ACCENT_PURPLE,
    HOME_PAGER_ACCENT_GREEN,
    HOME_PAGER_ACCENT_ORANGE,
} home_pager_accent_t;

typedef struct {
    uint16_t id;
    const char *title;
    const char *caption;
    const char *symbol;
    const void *image_src;
    home_pager_accent_t accent;
    bool disabled;
} home_pager_item_t;

typedef struct {
    const home_pager_item_t *items;
    uint8_t item_count;
} home_pager_page_t;

typedef enum {
    HOME_PAGER_EVENT_ITEM_SELECTED = 0,
    HOME_PAGER_EVENT_PAGE_CHANGED,
} home_pager_event_kind_t;

typedef struct {
    home_pager_event_kind_t kind;
    uint8_t page_index;
    uint16_t item_id;
} home_pager_event_t;

typedef void (*home_pager_event_fn)(
    void *user_ctx, const home_pager_event_t *event);

typedef struct {
    const home_pager_page_t *pages;
    uint8_t page_count;
    uint8_t initial_page;
    bool lazy_load;
    uint16_t top_inset;
    const lv_font_t *symbol_font;
    const lv_font_t *title_font;
    const lv_font_t *caption_font;
    home_pager_event_fn on_event;
    void *user_ctx;
} home_pager_config_t;

home_pager_t *home_pager_create(
    lv_obj_t *parent, const home_pager_config_t *config);
void home_pager_destroy(home_pager_t *pager);

lv_obj_t *home_pager_root(home_pager_t *pager);
void home_pager_set_page(
    home_pager_t *pager, uint8_t page_index, lv_anim_enable_t animation);
uint8_t home_pager_get_page(const home_pager_t *pager);
uint8_t home_pager_get_created_page_count(const home_pager_t *pager);
void home_pager_set_lazy_load(home_pager_t *pager, bool enabled);

#ifdef __cplusplus
}
#endif

#endif
