#ifndef PAGED_LIST_PAGED_LIST_H
#define PAGED_LIST_PAGED_LIST_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#if LVGL_VERSION_MAJOR < 9 || \
    (LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR < 5)
#error "paged_list requires LVGL 9.5 or newer"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* A bounded logical page replaces a potentially unbounded LVGL scroll area. */
typedef struct paged_list paged_list_t;

typedef struct {
    uint32_t generation;
    uint32_t first_index;
    uint32_t last_index;
} paged_list_range_t;

typedef lv_obj_t *(*paged_list_row_create_fn)(
    void *user_ctx, lv_obj_t *parent);

/* Return false when the item is not available in the application's cache yet. */
typedef bool (*paged_list_row_bind_fn)(
    void *user_ctx, lv_obj_t *row, uint32_t index, bool selected);
typedef void (*paged_list_row_selected_fn)(
    void *user_ctx, lv_obj_t *row, uint32_t index, bool selected);
typedef bool (*paged_list_item_ready_fn)(
    void *user_ctx, uint32_t index);

typedef void (*paged_list_request_range_fn)(
    void *user_ctx, const paged_list_range_t *range);

/*
 * Callbacks run while the list is updating its LVGL object tree. They may update
 * application state, but must defer destruction of this list or its ancestors
 * to the next UI safe turn.
 */

typedef enum {
    PAGED_LIST_EVENT_SELECTION_CHANGED = 0,
    PAGED_LIST_EVENT_ITEM_ACTIVATED,
    PAGED_LIST_EVENT_PAGE_CHANGED,
} paged_list_event_kind_t;

typedef struct {
    paged_list_event_kind_t kind;
    uint32_t index;
    uint32_t page_index;
    uint32_t page_count;
    bool ready;
} paged_list_event_t;

typedef void (*paged_list_event_fn)(
    void *user_ctx, const paged_list_event_t *event);

typedef struct {
    uint32_t previous_item;
    uint32_t next_item;
    uint32_t previous_page;
    uint32_t next_page;
    uint32_t activate;
} paged_list_keymap_t;

typedef struct {
    uint32_t item_count;
    uint32_t initial_index;
    uint16_t row_height;
    uint16_t row_gap;
    uint8_t visible_rows;
    uint8_t prefetch_pages;
    bool wrap_navigation;
    bool activate_on_click;
    lv_group_t *input_group;
    paged_list_row_create_fn create_row;
    paged_list_row_bind_fn bind_row;
    paged_list_row_selected_fn set_row_selected;
    paged_list_item_ready_fn item_ready;
    paged_list_request_range_fn request_range;
    paged_list_event_fn on_event;
    /* NULL selects the default LVGL arrow/Enter mapping. All zero disables it. */
    const paged_list_keymap_t *keymap;
    void *user_ctx;
} paged_list_config_t;

paged_list_t *paged_list_create(
    lv_obj_t *parent, const paged_list_config_t *config);
void paged_list_destroy(paged_list_t *list);

lv_obj_t *paged_list_root(paged_list_t *list);

uint32_t paged_list_item_count(const paged_list_t *list);
uint32_t paged_list_selected_index(const paged_list_t *list);
uint32_t paged_list_current_page(const paged_list_t *list);
uint32_t paged_list_page_count(const paged_list_t *list);
uint32_t paged_list_generation(const paged_list_t *list);

/* Navigation functions return true only when the selected index changes. */
bool paged_list_set_selected(
    paged_list_t *list, uint32_t index, bool notify);
bool paged_list_select_next(paged_list_t *list, bool notify);
bool paged_list_select_previous(paged_list_t *list, bool notify);
bool paged_list_set_page(
    paged_list_t *list, uint32_t page_index, bool notify);
bool paged_list_next_page(paged_list_t *list, bool notify);
bool paged_list_previous_page(paged_list_t *list, bool notify);

/* Reset invalidates old asynchronous results. The data source owns its cache. */
void paged_list_reset(
    paged_list_t *list, uint32_t item_count, uint32_t selected_index);

/*
 * Call from the UI thread after the data source updates an inclusive range.
 * Returns false for stale generations or invalid ranges.
 */
bool paged_list_data_changed(
    paged_list_t *list,
    uint32_t generation,
    uint32_t first_index,
    uint32_t last_index);
void paged_list_refresh(paged_list_t *list);
void paged_list_retry_visible(paged_list_t *list);

#ifdef __cplusplus
}
#endif

#endif
