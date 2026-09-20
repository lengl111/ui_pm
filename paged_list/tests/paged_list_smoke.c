#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <lvgl.h>

#include "paged_list/paged_list.h"

typedef struct {
    uint32_t created_rows;
    uint32_t bound_rows;
    uint32_t selection_updates;
    uint32_t requested_ranges;
    uint32_t last_generation;
    uint32_t last_first;
    uint32_t last_last;
    uint32_t selection_events;
    uint32_t page_events;
    uint32_t activation_events;
    bool ready[32];
    bool reenter_on_selection;
    paged_list_t *list;
    paged_list_event_kind_t event_kinds[8];
    uint32_t event_indices[8];
    uint8_t event_count;
} smoke_context_t;

static lv_obj_t *smoke_create_row(void *user_ctx, lv_obj_t *parent)
{
    smoke_context_t *context = user_ctx;
    lv_obj_t *row = lv_button_create(parent);

    ++context->created_rows;
    (void)lv_label_create(row);
    return row;
}

static bool smoke_bind_row(
    void *user_ctx, lv_obj_t *row, uint32_t index, bool selected)
{
    smoke_context_t *context = user_ctx;
    lv_obj_t *label = lv_obj_get_child(row, 0);

    ++context->bound_rows;
    lv_label_set_text_fmt(label, "%u%s", (unsigned)index,
                          selected ? "*" : "");
    return index < 32u && context->ready[index];
}

static bool smoke_item_ready(void *user_ctx, uint32_t index)
{
    smoke_context_t *context = user_ctx;

    return index < 32u && context->ready[index];
}

static void smoke_set_row_selected(
    void *user_ctx, lv_obj_t *row, uint32_t index, bool selected)
{
    smoke_context_t *context = user_ctx;

    (void)row;
    (void)index;
    (void)selected;
    ++context->selection_updates;
}

static void smoke_request_range(
    void *user_ctx, const paged_list_range_t *range)
{
    smoke_context_t *context = user_ctx;

    ++context->requested_ranges;
    context->last_generation = range->generation;
    context->last_first = range->first_index;
    context->last_last = range->last_index;
}

static void smoke_on_event(
    void *user_ctx, const paged_list_event_t *event)
{
    smoke_context_t *context = user_ctx;

    if(context->event_count < 8u) {
        context->event_kinds[context->event_count] = event->kind;
        context->event_indices[context->event_count] = event->index;
        ++context->event_count;
    }
    if(event->kind == PAGED_LIST_EVENT_SELECTION_CHANGED) {
        ++context->selection_events;
        if(context->reenter_on_selection && event->index == 6u) {
            context->reenter_on_selection = false;
            assert(paged_list_set_selected(context->list, 12u, true));
        }
    }
    else if(event->kind == PAGED_LIST_EVENT_PAGE_CHANGED) {
        ++context->page_events;
    }
    else if(event->kind == PAGED_LIST_EVENT_ITEM_ACTIVATED) {
        ++context->activation_events;
    }
}

int main(void)
{
    const paged_list_config_t config = {
        .item_count = 100000u,
        .row_height = 24u,
        .row_gap = 2u,
        .visible_rows = 6u,
        .prefetch_pages = 1u,
        .create_row = smoke_create_row,
        .bind_row = smoke_bind_row,
        .set_row_selected = smoke_set_row_selected,
        .item_ready = smoke_item_ready,
        .request_range = smoke_request_range,
        .on_event = smoke_on_event,
    };
    smoke_context_t context = {0};
    paged_list_config_t local_config = config;
    lv_display_t *display;
    paged_list_t *list;

    lv_init();
    display = lv_display_create(320, 240);
    assert(display != NULL);
    lv_display_set_default(display);
    local_config.user_ctx = &context;
    list = paged_list_create(lv_screen_active(), &local_config);
    assert(list != NULL);
    assert(context.created_rows == 6u);
    assert(context.bound_rows == 6u);
    assert(context.requested_ranges == 1u);
    assert(context.last_generation == 1u);
    assert(context.last_first == 0u && context.last_last == 11u);
    assert(paged_list_page_count(list) == 16667u);
    context.list = list;

    context.bound_rows = 0u;
    paged_list_refresh(list);
    assert(context.bound_rows == 6u);
    assert(context.requested_ranges == 1u);
    paged_list_retry_visible(list);
    assert(context.requested_ranges == 2u);

    context.bound_rows = 0u;
    assert(!paged_list_data_changed(list, 0u, 0u, 2u));
    assert(context.bound_rows == 0u);
    context.ready[0] = true;
    context.ready[1] = true;
    context.ready[2] = true;
    assert(paged_list_data_changed(list, 1u, 0u, 2u));
    assert(context.bound_rows == 3u);
    assert(context.requested_ranges == 3u);
    assert(context.last_first == 3u && context.last_last == 11u);

    for(uint32_t index = 3u; index <= 11u; ++index) {
        context.ready[index] = true;
    }
    context.bound_rows = 0u;
    assert(paged_list_data_changed(list, 1u, 3u, 11u));
    assert(context.bound_rows == 3u);
    assert(context.requested_ranges == 3u);
    context.bound_rows = 0u;
    assert(paged_list_data_changed(list, 1u, 9u, 11u));
    assert(context.bound_rows == 0u);

    context.bound_rows = 0u;
    context.selection_updates = 0u;
    assert(paged_list_set_selected(list, 1u, true));
    assert(context.bound_rows == 0u);
    assert(context.selection_updates == 2u);
    assert(paged_list_set_selected(list, 7u, true));
    assert(context.bound_rows == 6u);
    assert(paged_list_current_page(list) == 1u);
    assert(context.selection_events == 2u);
    assert(context.page_events == 1u);
    assert(context.last_first == 12u && context.last_last == 17u);

    assert(paged_list_set_selected(list, 13u, true));
    assert(context.last_first == 12u && context.last_last == 23u);
    assert(paged_list_set_selected(list, 19u, true));
    assert(context.last_first == 12u && context.last_last == 29u);

    assert(paged_list_set_page(list, 16666u, true));
    assert(paged_list_selected_index(list) == 99997u);
    assert(paged_list_current_page(list) == 16666u);
    assert(paged_list_select_next(list, true));
    assert(paged_list_selected_index(list) == 99998u);
    assert(paged_list_select_next(list, true));
    assert(paged_list_selected_index(list) == 99999u);
    assert(!paged_list_select_next(list, true));

    paged_list_reset(list, 2u, 99u);
    assert(paged_list_generation(list) == 2u);
    assert(paged_list_selected_index(list) == 1u);
    assert(paged_list_page_count(list) == 1u);
    assert(paged_list_data_changed(list, 2u, 0u, 1u));

    paged_list_reset(list, 20u, 0u);
    context.event_count = 0u;
    context.reenter_on_selection = true;
    assert(paged_list_set_selected(list, 6u, true));
    assert(paged_list_selected_index(list) == 12u);
    assert(context.event_count == 4u);
    assert(context.event_kinds[0] == PAGED_LIST_EVENT_SELECTION_CHANGED);
    assert(context.event_indices[0] == 6u);
    assert(context.event_kinds[1] == PAGED_LIST_EVENT_PAGE_CHANGED);
    assert(context.event_indices[1] == 6u);
    assert(context.event_kinds[2] == PAGED_LIST_EVENT_SELECTION_CHANGED);
    assert(context.event_indices[2] == 12u);
    assert(context.event_kinds[3] == PAGED_LIST_EVENT_PAGE_CHANGED);
    assert(context.event_indices[3] == 12u);

    paged_list_reset(list, UINT32_MAX, UINT32_MAX - 1u);
    assert(paged_list_selected_index(list) == UINT32_MAX - 1u);
    assert(paged_list_page_count(list) == 715827883u);
    assert(paged_list_set_page(list, 0u, true));
    assert(paged_list_selected_index(list) == 2u);
    assert(paged_list_set_selected(list, 5u, false));
    assert(paged_list_set_page(list, 715827882u, true));
    assert(paged_list_selected_index(list) == UINT32_MAX - 1u);

    paged_list_destroy(list);
    lv_deinit();
    puts("paged_list smoke passed");
    return 0;
}
