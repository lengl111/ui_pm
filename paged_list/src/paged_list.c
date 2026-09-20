#include "paged_list/paged_list.h"

#include <limits.h>

#define PAGED_LIST_EVENT_QUEUE_CAPACITY 8u

typedef struct {
    struct paged_list *list;
    lv_obj_t *row;
    uint32_t index;
    bool ready;
} paged_list_slot_t;

struct paged_list {
    paged_list_config_t config;
    lv_obj_t *root;
    paged_list_slot_t *slots;
    uint32_t selected_index;
    uint32_t generation;
    uint32_t requested_first;
    uint32_t requested_last;
    paged_list_keymap_t keymap;
    paged_list_event_t event_queue[PAGED_LIST_EVENT_QUEUE_CAPACITY];
    uint8_t event_head;
    uint8_t event_count;
    bool request_valid;
    bool rendering;
    bool dispatching_events;
};

static const paged_list_keymap_t paged_list_default_keymap = {
    .previous_item = LV_KEY_UP,
    .next_item = LV_KEY_DOWN,
    .previous_page = LV_KEY_LEFT,
    .next_page = LV_KEY_RIGHT,
    .activate = LV_KEY_ENTER,
};

static uint32_t paged_list_page_count_for(const paged_list_t *list)
{
    uint32_t rows;

    if(list == NULL || list->config.item_count == 0u) return 0u;
    rows = list->config.visible_rows;
    return ((list->config.item_count - 1u) / rows) + 1u;
}

static uint32_t paged_list_page_for(const paged_list_t *list, uint32_t index)
{
    if(list == NULL || list->config.visible_rows == 0u) return 0u;
    return index / list->config.visible_rows;
}

static uint32_t paged_list_page_start(const paged_list_t *list)
{
    return paged_list_page_for(list, list->selected_index) *
           list->config.visible_rows;
}

static void paged_list_dispatch_events(paged_list_t *list)
{
    paged_list_event_t event;

    if(list == NULL || list->dispatching_events ||
       list->config.on_event == NULL) return;
    list->dispatching_events = true;
    while(list->event_count > 0u) {
        event = list->event_queue[list->event_head];
        list->event_head = (uint8_t)(
            (list->event_head + 1u) % PAGED_LIST_EVENT_QUEUE_CAPACITY);
        --list->event_count;
        list->config.on_event(list->config.user_ctx, &event);
    }
    list->dispatching_events = false;
}

static bool paged_list_queue_event(
    paged_list_t *list,
    paged_list_event_kind_t kind,
    uint32_t index,
    bool ready)
{
    uint8_t tail;
    paged_list_event_t *event;

    if(list == NULL || list->config.on_event == NULL) return true;
    if(list->event_count >= PAGED_LIST_EVENT_QUEUE_CAPACITY) return false;
    tail = (uint8_t)((list->event_head + list->event_count) %
                     PAGED_LIST_EVENT_QUEUE_CAPACITY);
    event = &list->event_queue[tail];
    event->kind = kind;
    event->index = index;
    event->page_index = paged_list_page_for(list, index);
    event->page_count = paged_list_page_count_for(list);
    event->ready = ready;
    ++list->event_count;
    return true;
}

static bool paged_list_can_queue_events(
    const paged_list_t *list, uint8_t count)
{
    if(list == NULL || list->config.on_event == NULL) return true;
    return count <= PAGED_LIST_EVENT_QUEUE_CAPACITY - list->event_count;
}

static bool paged_list_selected_ready(const paged_list_t *list)
{
    uint8_t slot;

    if(list == NULL) return false;
    for(slot = 0u; slot < list->config.visible_rows; ++slot) {
        if(list->slots[slot].index == list->selected_index) {
            return list->slots[slot].ready;
        }
    }
    return false;
}

static paged_list_slot_t *paged_list_find_slot(
    paged_list_t *list, uint32_t index)
{
    uint8_t slot;

    if(list == NULL) return NULL;
    for(slot = 0u; slot < list->config.visible_rows; ++slot) {
        if(list->slots[slot].index == index) return &list->slots[slot];
    }
    return NULL;
}

static void paged_list_update_selection(
    paged_list_t *list, uint32_t index, bool selected)
{
    paged_list_slot_t *slot = paged_list_find_slot(list, index);

    if(slot == NULL) return;
    if(list->config.set_row_selected != NULL) {
        list->config.set_row_selected(
            list->config.user_ctx, slot->row, index, selected);
    }
    else {
        slot->ready = list->config.bind_row(
            list->config.user_ctx, slot->row, index, selected);
    }
}

static void paged_list_request_visible(paged_list_t *list)
{
    paged_list_range_t range;
    uint32_t start;
    uint32_t page_span;
    uint32_t index;
    bool missing;

    if(list == NULL || list->config.request_range == NULL ||
       list->config.item_count == 0u) return;

    page_span = (uint32_t)list->config.prefetch_pages *
                list->config.visible_rows;
    start = paged_list_page_start(list);
    range.first_index = start > page_span ? start - page_span : 0u;
    if(UINT32_MAX - start < list->config.visible_rows + page_span) {
        range.last_index = list->config.item_count - 1u;
    }
    else {
        range.last_index = start + list->config.visible_rows + page_span - 1u;
        if(range.last_index >= list->config.item_count) {
            range.last_index = list->config.item_count - 1u;
        }
    }
    range.generation = list->generation;

    if(list->config.item_ready != NULL) {
        missing = false;
        for(index = range.first_index; index <= range.last_index; ++index) {
            if(!list->config.item_ready(list->config.user_ctx, index)) {
                range.first_index = index;
                missing = true;
                break;
            }
            if(index == UINT32_MAX) break;
        }
        if(!missing) {
            list->request_valid = false;
            return;
        }
        for(index = range.last_index; index > range.first_index; --index) {
            if(!list->config.item_ready(list->config.user_ctx, index)) {
                range.last_index = index;
                break;
            }
        }
    }

    /* A selection move within the same requested window is not a new load. */
    if(list->request_valid &&
       range.first_index >= list->requested_first &&
       range.last_index <= list->requested_last) return;

    list->request_valid = true;
    list->requested_first = range.first_index;
    list->requested_last = range.last_index;
    list->config.request_range(list->config.user_ctx, &range);
}

static void paged_list_render(paged_list_t *list)
{
    uint32_t start;
    uint8_t slot;

    if(list == NULL || list->rendering) return;
    list->rendering = true;
    start = paged_list_page_start(list);

    for(slot = 0u; slot < list->config.visible_rows; ++slot) {
        paged_list_slot_t *current = &list->slots[slot];
        uint32_t index;

        if(start >= list->config.item_count ||
           slot >= list->config.item_count - start) {
            current->index = UINT32_MAX;
            current->ready = false;
            lv_obj_add_flag(current->row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        index = start + slot;
        current->index = index;
        current->ready = list->config.bind_row(
            list->config.user_ctx,
            current->row,
            index,
            index == list->selected_index);
        lv_obj_clear_flag(current->row, LV_OBJ_FLAG_HIDDEN);
    }

    list->rendering = false;
    paged_list_request_visible(list);
}

static bool paged_list_move_to_internal(
    paged_list_t *list,
    uint32_t index,
    bool notify,
    bool dispatch,
    uint8_t reserved_events)
{
    uint32_t old_index;
    uint32_t old_page;
    uint32_t new_page;
    uint8_t event_count;
    bool changed;

    if(list == NULL || list->config.item_count == 0u ||
       index >= list->config.item_count || list->rendering) return false;
    old_index = list->selected_index;
    old_page = paged_list_page_for(list, old_index);
    changed = list->selected_index != index;
    new_page = paged_list_page_for(list, index);
    event_count = reserved_events;
    if(changed && notify && list->config.on_event != NULL) {
        event_count = (uint8_t)(event_count + 1u +
                                (old_page != new_page ? 1u : 0u));
    }
    if(!paged_list_can_queue_events(list, event_count)) return false;
    list->selected_index = index;
    if(old_page != new_page) {
        paged_list_render(list);
    }
    else if(changed) {
        paged_list_update_selection(list, old_index, false);
        paged_list_update_selection(list, index, true);
    }
    if(changed && notify) {
        (void)paged_list_queue_event(
            list, PAGED_LIST_EVENT_SELECTION_CHANGED, index,
            paged_list_selected_ready(list));
        if(old_page != new_page) {
            (void)paged_list_queue_event(
                list, PAGED_LIST_EVENT_PAGE_CHANGED, index,
                paged_list_selected_ready(list));
        }
    }
    if(dispatch) paged_list_dispatch_events(list);
    return changed;
}

static bool paged_list_move_to(
    paged_list_t *list, uint32_t index, bool notify)
{
    return paged_list_move_to_internal(list, index, notify, true, 0u);
}

static void paged_list_on_row_clicked(lv_event_t *event)
{
    paged_list_slot_t *slot = lv_event_get_user_data(event);
    paged_list_t *list;
    uint32_t index;
    uint8_t required_events;
    bool ready;

    if(slot == NULL || slot->index == UINT32_MAX) return;
    list = slot->list;
    if(list == NULL) return;
    index = slot->index;
    ready = slot->ready;
    required_events = 0u;
    if(list->selected_index != index && list->config.on_event != NULL) {
        required_events = (uint8_t)(
            1u + (paged_list_page_for(list, list->selected_index) !=
                          paged_list_page_for(list, index)
                      ? 1u
                      : 0u));
    }
    if(list->config.activate_on_click && ready &&
       list->config.on_event != NULL) {
        ++required_events;
    }
    if(!paged_list_can_queue_events(list, required_events)) return;
    if(!paged_list_move_to_internal(
           list, index, true, false,
           list->config.activate_on_click && ready ? 1u : 0u) &&
       list->selected_index != index) return;
    if(list->config.activate_on_click && ready) {
        (void)paged_list_queue_event(
            list, PAGED_LIST_EVENT_ITEM_ACTIVATED, index, true);
    }
    paged_list_dispatch_events(list);
}

static void paged_list_on_root_event(lv_event_t *event)
{
    paged_list_t *list = lv_event_get_user_data(event);
    uint32_t key;

    if(list == NULL) return;
    if(lv_event_get_code(event) == LV_EVENT_DELETE) {
        if(list->config.input_group != NULL) {
            lv_group_remove_obj(list->root);
        }
        lv_free(list->slots);
        lv_free(list);
        return;
    }
    if(lv_event_get_code(event) != LV_EVENT_KEY) return;

    key = lv_event_get_key(event);
    if(list->keymap.previous_item != 0u &&
       key == list->keymap.previous_item) {
        (void)paged_list_select_previous(list, true);
    }
    else if(list->keymap.next_item != 0u &&
            key == list->keymap.next_item) {
        (void)paged_list_select_next(list, true);
    }
    else if(list->keymap.previous_page != 0u &&
            key == list->keymap.previous_page) {
        (void)paged_list_previous_page(list, true);
    }
    else if(list->keymap.next_page != 0u &&
            key == list->keymap.next_page) {
        (void)paged_list_next_page(list, true);
    }
    else if(list->keymap.activate != 0u && key == list->keymap.activate &&
            list->config.item_count > 0u &&
            paged_list_selected_ready(list)) {
        (void)paged_list_queue_event(
            list, PAGED_LIST_EVENT_ITEM_ACTIVATED,
            list->selected_index, true);
        paged_list_dispatch_events(list);
    }
}

paged_list_t *paged_list_create(
    lv_obj_t *parent, const paged_list_config_t *config)
{
    paged_list_t *list;
    uint8_t slot;

    if(parent == NULL || config == NULL || config->visible_rows == 0u ||
       config->row_height == 0u || config->create_row == NULL ||
       config->bind_row == NULL) return NULL;

    list = lv_malloc_zeroed(sizeof(*list));
    if(list == NULL) return NULL;
    list->config = *config;
    list->keymap = config->keymap != NULL
                       ? *config->keymap
                       : paged_list_default_keymap;
    list->slots = lv_malloc_zeroed(
        (size_t)config->visible_rows * sizeof(*list->slots));
    if(list->slots == NULL) {
        lv_free(list);
        return NULL;
    }
    list->generation = 1u;
    if(config->item_count > 0u) {
        list->selected_index = config->initial_index < config->item_count
                                   ? config->initial_index
                                   : config->item_count - 1u;
    }

    list->root = lv_obj_create(parent);
    if(list->root == NULL) {
        lv_free(list->slots);
        lv_free(list);
        return NULL;
    }
    lv_obj_remove_style_all(list->root);
    lv_obj_set_size(list->root, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(list->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(list->root, paged_list_on_root_event,
                        LV_EVENT_ALL, list);

    for(slot = 0u; slot < config->visible_rows; ++slot) {
        paged_list_slot_t *current = &list->slots[slot];

        current->list = list;
        current->index = UINT32_MAX;
        current->row = config->create_row(config->user_ctx, list->root);
        if(current->row == NULL) {
            lv_obj_delete(list->root);
            return NULL;
        }
        lv_obj_set_size(current->row, LV_PCT(100), config->row_height);
        lv_obj_set_pos(current->row, 0,
                       slot * (config->row_height + config->row_gap));
        lv_obj_add_event_cb(current->row, paged_list_on_row_clicked,
                            LV_EVENT_CLICKED, current);
    }
    if(config->input_group != NULL) lv_group_add_obj(config->input_group, list->root);
    paged_list_render(list);
    return list;
}

void paged_list_destroy(paged_list_t *list)
{
    if(list != NULL && list->root != NULL) lv_obj_delete(list->root);
}

lv_obj_t *paged_list_root(paged_list_t *list)
{
    return list != NULL ? list->root : NULL;
}

uint32_t paged_list_item_count(const paged_list_t *list)
{
    return list != NULL ? list->config.item_count : 0u;
}

uint32_t paged_list_selected_index(const paged_list_t *list)
{
    return list != NULL ? list->selected_index : 0u;
}

uint32_t paged_list_current_page(const paged_list_t *list)
{
    return paged_list_page_for(list, list != NULL ? list->selected_index : 0u);
}

uint32_t paged_list_page_count(const paged_list_t *list)
{
    return paged_list_page_count_for(list);
}

uint32_t paged_list_generation(const paged_list_t *list)
{
    return list != NULL ? list->generation : 0u;
}

bool paged_list_set_selected(paged_list_t *list, uint32_t index, bool notify)
{
    return paged_list_move_to(list, index, notify);
}

bool paged_list_select_next(paged_list_t *list, bool notify)
{
    if(list == NULL || list->config.item_count == 0u) return false;
    if(list->selected_index < list->config.item_count - 1u) {
        return paged_list_move_to(list, list->selected_index + 1u, notify);
    }
    if(list->config.wrap_navigation) return paged_list_move_to(list, 0u, notify);
    return false;
}

bool paged_list_select_previous(paged_list_t *list, bool notify)
{
    if(list == NULL || list->config.item_count == 0u) return false;
    if(list->selected_index > 0u) {
        return paged_list_move_to(list, list->selected_index - 1u, notify);
    }
    if(list->config.wrap_navigation) {
        return paged_list_move_to(list, list->config.item_count - 1u, notify);
    }
    return false;
}

bool paged_list_set_page(paged_list_t *list, uint32_t page_index, bool notify)
{
    uint32_t base;
    uint32_t page_count;
    uint32_t row;
    uint32_t index;

    if(list == NULL || list->config.item_count == 0u) return false;
    page_count = paged_list_page_count_for(list);
    if(page_index >= page_count) return false;
    row = list->selected_index % list->config.visible_rows;
    base = page_index * list->config.visible_rows;
    if(row >= list->config.item_count - base) {
        index = list->config.item_count - 1u;
    }
    else {
        index = base + row;
    }
    return paged_list_move_to(list, index, notify);
}

bool paged_list_next_page(paged_list_t *list, bool notify)
{
    uint32_t page;
    uint32_t count;

    if(list == NULL) return false;
    page = paged_list_current_page(list);
    count = paged_list_page_count_for(list);
    if(count == 0u) return false;
    if(page < count - 1u) return paged_list_set_page(list, page + 1u, notify);
    if(list->config.wrap_navigation && count > 0u) {
        return paged_list_set_page(list, 0u, notify);
    }
    return false;
}

bool paged_list_previous_page(paged_list_t *list, bool notify)
{
    uint32_t page;
    uint32_t count;

    if(list == NULL) return false;
    page = paged_list_current_page(list);
    count = paged_list_page_count_for(list);
    if(page > 0u) return paged_list_set_page(list, page - 1u, notify);
    if(list->config.wrap_navigation && count > 0u) {
        return paged_list_set_page(list, count - 1u, notify);
    }
    return false;
}

void paged_list_reset(
    paged_list_t *list, uint32_t item_count, uint32_t selected_index)
{
    if(list == NULL) return;
    list->config.item_count = item_count;
    ++list->generation;
    if(list->generation == 0u) list->generation = 1u;
    list->request_valid = false;
    list->event_head = 0u;
    list->event_count = 0u;
    if(item_count == 0u) list->selected_index = 0u;
    else list->selected_index = selected_index < item_count
                                    ? selected_index
                                    : item_count - 1u;
    paged_list_render(list);
}

bool paged_list_data_changed(
    paged_list_t *list,
    uint32_t generation,
    uint32_t first_index,
    uint32_t last_index)
{
    uint8_t slot;

    if(list == NULL || generation != list->generation ||
       list->config.item_count == 0u || first_index > last_index ||
       first_index >= list->config.item_count) return false;
    if(last_index >= list->config.item_count) {
        last_index = list->config.item_count - 1u;
    }
    for(slot = 0u; slot < list->config.visible_rows; ++slot) {
        paged_list_slot_t *current = &list->slots[slot];

        if(current->index == UINT32_MAX || current->index < first_index ||
           current->index > last_index) continue;
        current->ready = list->config.bind_row(
            list->config.user_ctx,
            current->row,
            current->index,
            current->index == list->selected_index);
    }
    if(list->config.item_ready != NULL) {
        list->request_valid = false;
        paged_list_request_visible(list);
    }
    return true;
}

void paged_list_refresh(paged_list_t *list)
{
    paged_list_render(list);
}

void paged_list_retry_visible(paged_list_t *list)
{
    if(list == NULL) return;
    list->request_valid = false;
    paged_list_request_visible(list);
}
