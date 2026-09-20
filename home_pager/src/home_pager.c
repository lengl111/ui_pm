#include "home_pager/home_pager.h"

#include <stddef.h>
#include <string.h>

#define HOME_PAGER_NO_PAGE UINT8_MAX
#define HOME_PAGER_MAX_PHYSICAL_PAGES (HOME_PAGER_MAX_PAGES + 2u)
#define HOME_PAGER_INDICATOR_MIN_WIDTH 8
#define HOME_PAGER_INDICATOR_MAX_WIDTH 24
#define HOME_PAGER_INDICATOR_PITCH 18
#define HOME_PAGER_RELEASE_THRESHOLD_PER_MILLE 200
#define HOME_PAGER_FLICK_VELOCITY 5

typedef struct {
    struct home_pager *pager;
    uint16_t item_id;
    uint8_t page_index;
} home_pager_item_binding_t;

struct home_pager {
    lv_obj_t *root;
    lv_obj_t *carousel;
    lv_obj_t *indicator_track;
    lv_obj_t *page_objects[HOME_PAGER_MAX_PHYSICAL_PAGES];
    lv_obj_t *indicators[HOME_PAGER_MAX_PAGES];
    home_pager_page_t pages[HOME_PAGER_MAX_PAGES];
    home_pager_item_t items[HOME_PAGER_MAX_PAGES]
                                [HOME_PAGER_MAX_ITEMS_PER_PAGE];
    home_pager_item_binding_t bindings[HOME_PAGER_MAX_PHYSICAL_PAGES]
                                           [HOME_PAGER_MAX_ITEMS_PER_PAGE];
    bool page_created[HOME_PAGER_MAX_PHYSICAL_PAGES];
    bool lazy_load;
    bool programmatic_scroll;
    bool suppress_scroll_end;
    bool correcting_snap;
    bool scroll_active;
    bool geometry_valid;
    bool release_page_pending;
    bool geometry_refreshing;
    uint8_t page_count;
    uint8_t physical_page_count;
    uint8_t current_page;
    uint8_t target_physical_page;
    uint8_t release_physical_page;
    uint8_t gesture_start_physical_page;
    int32_t snap_positions[HOME_PAGER_MAX_PHYSICAL_PAGES];
    uint8_t indicator_widths[HOME_PAGER_MAX_PAGES];
    lv_opa_t indicator_opacities[HOME_PAGER_MAX_PAGES];
    int32_t scroll_min;
    int32_t scroll_max;
    uint16_t top_inset;
    const lv_font_t *symbol_font;
    const lv_font_t *title_font;
    const lv_font_t *caption_font;
    home_pager_event_fn on_event;
    void *user_ctx;
};

static const int32_t four_item_columns[] = {
    LV_GRID_FR(9), LV_GRID_FR(8), LV_GRID_FR(8), LV_GRID_TEMPLATE_LAST};
static const int32_t three_item_columns[] = {
    LV_GRID_FR(10), LV_GRID_FR(15), LV_GRID_TEMPLATE_LAST};
static const int32_t two_item_columns[] = {
    LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
static const int32_t one_item_columns[] = {
    LV_GRID_FR(1), LV_GRID_FR(2), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
static const int32_t two_rows[] = {
    LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
static const int32_t one_row[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

static void home_pager_create_page_content(
    home_pager_t *pager, uint8_t physical_page_index);
static bool home_pager_refresh_geometry(home_pager_t *pager);
static uint8_t home_pager_nearest_page(home_pager_t *pager);
static void home_pager_prepare_neighbors(
    home_pager_t *pager, uint8_t page_index);
static void home_pager_prepare_transition(
    home_pager_t *pager, uint8_t from_page, uint8_t to_page);
static void home_pager_update_indicators(home_pager_t *pager);
static void home_pager_on_scroll_throw_begin(lv_event_t *event);
static void home_pager_on_size_changed(lv_event_t *event);
static bool home_pager_scroll_to_page(
    home_pager_t *pager, uint8_t page_index, lv_anim_enable_t animation);

static lv_color_t home_pager_accent_color(home_pager_accent_t accent)
{
    switch(accent) {
        case HOME_PAGER_ACCENT_BLUE:
            return lv_color_hex(0x3388F4);
        case HOME_PAGER_ACCENT_PINK:
            return lv_color_hex(0xF866A5);
        case HOME_PAGER_ACCENT_PURPLE:
            return lv_color_hex(0x8869E8);
        case HOME_PAGER_ACCENT_GREEN:
            return lv_color_hex(0x55D66B);
        case HOME_PAGER_ACCENT_ORANGE:
            return lv_color_hex(0xFF8A34);
        case HOME_PAGER_ACCENT_YELLOW:
        default:
            return lv_color_hex(0xFFD72E);
    }
}

static lv_color_t home_pager_foreground_color(home_pager_accent_t accent)
{
    if(accent == HOME_PAGER_ACCENT_YELLOW ||
       accent == HOME_PAGER_ACCENT_GREEN ||
       accent == HOME_PAGER_ACCENT_ORANGE) {
        return lv_color_hex(0x17212B);
    }
    return lv_color_hex(0xFFFFFF);
}

static const lv_font_t *home_pager_font_or_default(const lv_font_t *font)
{
    return font != NULL ? font : LV_FONT_DEFAULT;
}

static uint8_t home_pager_logical_page(
    const home_pager_t *pager, uint8_t physical_page_index)
{
    if(pager->page_count <= 1u) return 0u;
    if(physical_page_index == 0u) return pager->page_count - 1u;
    if(physical_page_index >= pager->physical_page_count - 1u) return 0u;
    return physical_page_index - 1u;
}

static uint8_t home_pager_physical_page(
    const home_pager_t *pager, uint8_t logical_page_index)
{
    if(pager->page_count <= 1u) return 0u;
    return logical_page_index + 1u;
}

static bool home_pager_is_sentinel_page(
    const home_pager_t *pager, uint8_t physical_page_index)
{
    return pager->page_count > 1u &&
           (physical_page_index == 0u ||
            physical_page_index == pager->physical_page_count - 1u);
}

static void home_pager_make_plain(lv_obj_t *object)
{
    lv_obj_remove_style_all(object);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

static void home_pager_emit(
    home_pager_t *pager,
    home_pager_event_kind_t kind,
    uint8_t page_index,
    uint16_t item_id)
{
    home_pager_event_t event;

    if(pager->on_event == NULL) return;
    event.kind = kind;
    event.page_index = page_index;
    event.item_id = item_id;
    pager->on_event(pager->user_ctx, &event);
}

static void home_pager_on_root_delete(lv_event_t *event)
{
    home_pager_t *pager = lv_event_get_user_data(event);

    pager->root = NULL;
    lv_free(pager);
}

static void home_pager_on_item_clicked(lv_event_t *event)
{
    home_pager_item_binding_t *binding = lv_event_get_user_data(event);

    home_pager_emit(
        binding->pager,
        HOME_PAGER_EVENT_ITEM_SELECTED,
        binding->page_index,
        binding->item_id);
}

static void home_pager_place_card(
    lv_obj_t *card, uint8_t item_count, uint8_t item_index)
{
    if(item_count >= 4u) {
        if(item_index == 0u) {
            lv_obj_set_grid_cell(
                card, LV_GRID_ALIGN_STRETCH, 0, 1,
                LV_GRID_ALIGN_STRETCH, 0, 2);
        }
        else if(item_index == 1u) {
            lv_obj_set_grid_cell(
                card, LV_GRID_ALIGN_STRETCH, 1, 1,
                LV_GRID_ALIGN_STRETCH, 0, 2);
        }
        else {
            lv_obj_set_grid_cell(
                card, LV_GRID_ALIGN_STRETCH, 2, 1,
                LV_GRID_ALIGN_STRETCH, item_index - 2u, 1);
        }
    }
    else if(item_count == 3u) {
        if(item_index == 0u) {
            lv_obj_set_grid_cell(
                card, LV_GRID_ALIGN_STRETCH, 0, 1,
                LV_GRID_ALIGN_STRETCH, 0, 2);
        }
        else {
            lv_obj_set_grid_cell(
                card, LV_GRID_ALIGN_STRETCH, 1, 1,
                LV_GRID_ALIGN_STRETCH, item_index - 1u, 1);
        }
    }
    else if(item_count == 2u) {
        lv_obj_set_grid_cell(
            card, LV_GRID_ALIGN_STRETCH, item_index, 1,
            LV_GRID_ALIGN_STRETCH, 0, 1);
    }
    else {
        lv_obj_set_grid_cell(
            card, LV_GRID_ALIGN_STRETCH, 1, 1,
            LV_GRID_ALIGN_STRETCH, 0, 1);
    }
}

static void home_pager_add_card_content(
    home_pager_t *pager,
    lv_obj_t *card,
    const home_pager_item_t *item,
    bool compact)
{
    lv_color_t foreground = home_pager_foreground_color(item->accent);

    if(item->image_src != NULL) {
        lv_obj_t *image = lv_image_create(card);
        lv_obj_set_size(image, LV_PCT(94), LV_PCT(72));
        lv_image_set_src(image, item->image_src);
        lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
        lv_obj_set_flex_grow(image, 1u);
    }
    else {
        lv_obj_t *symbol = lv_label_create(card);
        lv_label_set_text(
            symbol,
            item->symbol != NULL ? item->symbol : LV_SYMBOL_IMAGE);
        lv_obj_set_style_text_color(symbol, foreground, 0);
        lv_obj_set_style_text_font(
            symbol, home_pager_font_or_default(pager->symbol_font), 0);
    }

    if(item->title != NULL && item->title[0] != '\0') {
        lv_obj_t *title = lv_label_create(card);
        lv_obj_set_width(title, LV_PCT(100));
        lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
        lv_label_set_text(title, item->title);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(title, foreground, 0);
        lv_obj_set_style_text_font(
            title, home_pager_font_or_default(pager->title_font), 0);
    }

    if(!compact && item->caption != NULL && item->caption[0] != '\0') {
        lv_obj_t *caption = lv_label_create(card);
        lv_obj_set_width(caption, LV_PCT(100));
        lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP);
        lv_label_set_text(caption, item->caption);
        lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(caption, foreground, 0);
        lv_obj_set_style_text_opa(caption, LV_OPA_70, 0);
        lv_obj_set_style_text_font(
            caption, home_pager_font_or_default(pager->caption_font), 0);
    }
}

static lv_obj_t *home_pager_create_card(
    home_pager_t *pager,
    lv_obj_t *page,
    uint8_t physical_page_index,
    uint8_t item_index)
{
    uint8_t logical_page_index =
        home_pager_logical_page(pager, physical_page_index);
    const home_pager_item_t *item =
        &pager->items[logical_page_index][item_index];
    home_pager_item_binding_t *binding =
        &pager->bindings[physical_page_index][item_index];
    lv_obj_t *card = lv_button_create(page);
    bool compact = pager->pages[logical_page_index].item_count >= 3u &&
                   item_index >=
                       (pager->pages[logical_page_index].item_count - 2u);

    binding->pager = pager;
    binding->item_id = item->id;
    binding->page_index = logical_page_index;

    home_pager_place_card(
        card, pager->pages[logical_page_index].item_count, item_index);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_bg_color(
        card, home_pager_accent_color(item->accent), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, compact ? 10 : 16, 0);
    lv_obj_set_style_pad_row(card, compact ? 6 : 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        card,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_opa(card, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_add_event_cb(
        card, home_pager_on_item_clicked, LV_EVENT_CLICKED, binding);

    if(item->disabled) {
        lv_obj_add_state(card, LV_STATE_DISABLED);
        lv_obj_set_style_opa(card, LV_OPA_40, LV_STATE_DISABLED);
    }
    home_pager_add_card_content(pager, card, item, compact);
    return card;
}

static void home_pager_create_page_content(
    home_pager_t *pager, uint8_t physical_page_index)
{
    lv_obj_t *page;
    uint8_t item_count;
    uint8_t logical_page_index;

    if(physical_page_index >= pager->physical_page_count ||
       pager->page_created[physical_page_index]) {
        return;
    }

    logical_page_index =
        home_pager_logical_page(pager, physical_page_index);
    page = pager->page_objects[physical_page_index];
    item_count = pager->pages[logical_page_index].item_count;
    if(item_count >= 4u) {
        lv_obj_set_grid_dsc_array(page, four_item_columns, two_rows);
    }
    else if(item_count == 3u) {
        lv_obj_set_grid_dsc_array(page, three_item_columns, two_rows);
    }
    else if(item_count == 2u) {
        lv_obj_set_grid_dsc_array(page, two_item_columns, one_row);
    }
    else {
        lv_obj_set_grid_dsc_array(page, one_item_columns, one_row);
    }

    for(uint8_t item_index = 0u; item_index < item_count; ++item_index) {
        (void)home_pager_create_card(
            pager, page, physical_page_index, item_index);
    }
    pager->page_created[physical_page_index] = true;
}

static bool home_pager_refresh_geometry(home_pager_t *pager)
{
    int32_t viewport_width;
    int32_t scroll_max;

    if(pager == NULL || pager->carousel == NULL || pager->page_count == 0u) {
        return false;
    }
    if(pager->geometry_refreshing) return pager->geometry_valid;
    pager->geometry_refreshing = true;
    lv_obj_update_layout(pager->carousel);
    viewport_width = lv_obj_get_content_width(pager->carousel);
    if(viewport_width <= 0) {
        pager->geometry_refreshing = false;
        return false;
    }

    scroll_max = lv_obj_get_scroll_left(pager->carousel) +
                 lv_obj_get_scroll_right(pager->carousel);
    if(scroll_max < 0) scroll_max = 0;

    for(uint8_t physical_page_index = 0u;
        physical_page_index < pager->physical_page_count;
        ++physical_page_index) {
        int32_t page_width;
        int32_t page_x;
        int32_t snap_x;

        if(pager->page_objects[physical_page_index] == NULL) {
            pager->geometry_refreshing = false;
            return false;
        }
        page_width = lv_obj_get_width(pager->page_objects[physical_page_index]);
        page_x = lv_obj_get_x(pager->page_objects[physical_page_index]);
        switch(lv_obj_get_scroll_snap_x(pager->carousel)) {
            case LV_SCROLL_SNAP_END:
                snap_x = page_x + page_width - viewport_width;
                break;
            case LV_SCROLL_SNAP_CENTER:
                snap_x = page_x + (page_width - viewport_width) / 2;
                break;
            case LV_SCROLL_SNAP_START:
            default:
                snap_x = page_x;
                break;
        }
        if(snap_x < 0) snap_x = 0;
        if(snap_x > scroll_max) snap_x = scroll_max;
        pager->snap_positions[physical_page_index] = snap_x;
    }

    pager->scroll_min = 0;
    pager->scroll_max = scroll_max;
    pager->geometry_valid = true;
    pager->geometry_refreshing = false;
    return true;
}

static int32_t home_pager_clamped_scroll_x(home_pager_t *pager)
{
    int32_t scroll_x = lv_obj_get_scroll_x(pager->carousel);

    if(scroll_x < pager->scroll_min) return pager->scroll_min;
    if(scroll_x > pager->scroll_max) return pager->scroll_max;
    return scroll_x;
}

static void home_pager_get_scroll_progress(
    home_pager_t *pager,
    uint8_t *base_page,
    uint8_t *next_page,
    int32_t *numerator,
    int32_t *denominator)
{
    int32_t scroll_x;

    *base_page = 0u;
    *next_page = HOME_PAGER_NO_PAGE;
    *numerator = 0;
    *denominator = 1;
    if(pager->page_count <= 1u) return;

    scroll_x = home_pager_clamped_scroll_x(pager);
    if(scroll_x <= pager->snap_positions[0]) {
        *base_page = home_pager_logical_page(pager, 0u);
        return;
    }
    if(scroll_x >= pager->snap_positions[pager->physical_page_count - 1u]) {
        *base_page = home_pager_logical_page(
            pager, pager->physical_page_count - 1u);
        return;
    }

    for(uint8_t physical_page_index = 0u;
        physical_page_index + 1u < pager->physical_page_count;
        ++physical_page_index) {
        int32_t start = pager->snap_positions[physical_page_index];
        int32_t end = pager->snap_positions[physical_page_index + 1u];

        if(end <= start) continue;
        if(scroll_x < end) {
            *base_page =
                home_pager_logical_page(pager, physical_page_index);
            *next_page = home_pager_logical_page(
                pager, physical_page_index + 1u);
            *numerator = scroll_x - start;
            *denominator = end - start;
            if(*numerator < 0) *numerator = 0;
            if(*numerator > *denominator) *numerator = *denominator;
            return;
        }
    }

    *base_page = home_pager_logical_page(
        pager, pager->physical_page_count - 1u);
}

static void home_pager_set_indicator_visual(
    home_pager_t *pager,
    uint8_t page_index,
    uint8_t width,
    lv_opa_t opacity)
{
    lv_obj_t *indicator;
    int32_t x;

    if(pager->indicator_track == NULL ||
       page_index >= pager->page_count) {
        return;
    }
    indicator = pager->indicators[page_index];
    x = (int32_t)page_index * HOME_PAGER_INDICATOR_PITCH +
        (HOME_PAGER_INDICATOR_MAX_WIDTH - width) / 2;
    if(pager->indicator_widths[page_index] != width) {
        pager->indicator_widths[page_index] = width;
        lv_obj_set_width(indicator, width);
        lv_obj_set_x(indicator, x);
    }
    if(pager->indicator_opacities[page_index] != opacity) {
        pager->indicator_opacities[page_index] = opacity;
        lv_obj_set_style_bg_opa(indicator, opacity, 0);
    }
}

static void home_pager_update_indicators(home_pager_t *pager)
{
    uint8_t base_page;
    uint8_t next_page;
    int32_t numerator;
    int32_t denominator;

    if(pager == NULL || pager->indicator_track == NULL) return;
    if(!pager->geometry_valid && !home_pager_refresh_geometry(pager)) return;

    home_pager_get_scroll_progress(
        pager, &base_page, &next_page, &numerator, &denominator);

    for(uint8_t index = 0u; index < pager->page_count; ++index) {
        uint8_t width = HOME_PAGER_INDICATOR_MIN_WIDTH;
        lv_opa_t opacity = LV_OPA_40;

        if(index == base_page) {
            width = (uint8_t)(HOME_PAGER_INDICATOR_MAX_WIDTH -
                              numerator *
                                  (HOME_PAGER_INDICATOR_MAX_WIDTH -
                                   HOME_PAGER_INDICATOR_MIN_WIDTH) /
                                  denominator);
            opacity = (lv_opa_t)(LV_OPA_80 -
                                 numerator * (LV_OPA_80 - LV_OPA_40) /
                                     denominator);
        }
        else if(index == next_page) {
            width = (uint8_t)(HOME_PAGER_INDICATOR_MIN_WIDTH +
                              numerator *
                                  (HOME_PAGER_INDICATOR_MAX_WIDTH -
                                   HOME_PAGER_INDICATOR_MIN_WIDTH) /
                                  denominator);
            opacity = (lv_opa_t)(LV_OPA_40 +
                                 numerator * (LV_OPA_80 - LV_OPA_40) /
                                     denominator);
        }
        home_pager_set_indicator_visual(pager, index, width, opacity);
    }
}

static uint8_t home_pager_nearest_page(home_pager_t *pager)
{
    int32_t scroll_x;
    int32_t best_distance = LV_COORD_MAX;
    uint8_t page_index = 0u;

    if(pager == NULL || pager->page_count == 0u) return 0u;
    if(!pager->geometry_valid && !home_pager_refresh_geometry(pager)) return 0u;
    scroll_x = home_pager_clamped_scroll_x(pager);
    for(uint8_t index = 0u;
        index < pager->physical_page_count;
        ++index) {
        int32_t distance = LV_ABS(scroll_x - pager->snap_positions[index]);

        if(distance <= best_distance) {
            best_distance = distance;
            page_index = index;
        }
    }
    return page_index;
}

static void home_pager_prepare_neighbors(
    home_pager_t *pager, uint8_t physical_page_index)
{
    if(!pager->lazy_load || pager->page_count == 0u ||
       physical_page_index >= pager->physical_page_count) {
        return;
    }
    home_pager_create_page_content(pager, physical_page_index);
    if(physical_page_index > 0u) {
        home_pager_create_page_content(pager, physical_page_index - 1u);
    }
    if(physical_page_index + 1u < pager->physical_page_count) {
        home_pager_create_page_content(pager, physical_page_index + 1u);
    }
}

static void home_pager_prepare_transition(
    home_pager_t *pager,
    uint8_t from_physical_page,
    uint8_t to_physical_page)
{
    uint8_t first_page;
    uint8_t last_page;

    if(!pager->lazy_load ||
       from_physical_page >= pager->physical_page_count ||
       to_physical_page >= pager->physical_page_count) {
        return;
    }
    first_page = from_physical_page < to_physical_page
                     ? from_physical_page
                     : to_physical_page;
    last_page = from_physical_page > to_physical_page
                    ? from_physical_page
                    : to_physical_page;
    for(uint8_t page_index = first_page;
        page_index <= last_page;
        ++page_index) {
        home_pager_create_page_content(pager, page_index);
    }
}

static uint8_t home_pager_release_page(
    home_pager_t *pager, int32_t velocity_x)
{
    uint8_t start_page = pager->gesture_start_physical_page;
    int32_t start_x = pager->snap_positions[start_page];
    int32_t scroll_x = lv_obj_get_scroll_x(pager->carousel);

    if(velocity_x < -HOME_PAGER_FLICK_VELOCITY) {
        if(start_page + 1u < pager->physical_page_count) {
            return start_page + 1u;
        }
        return start_page;
    }
    if(velocity_x > HOME_PAGER_FLICK_VELOCITY) {
        if(start_page > 0u) return start_page - 1u;
        return start_page;
    }
    if(scroll_x > start_x && start_page + 1u < pager->physical_page_count) {
        int32_t distance = scroll_x - start_x;
        int32_t span = pager->snap_positions[start_page + 1u] - start_x;

        if(span > 0 &&
           (int64_t)distance * 1000 >=
               (int64_t)span *
                   HOME_PAGER_RELEASE_THRESHOLD_PER_MILLE) {
            return start_page + 1u;
        }
    }
    else if(scroll_x < start_x && start_page > 0u) {
        int32_t distance = start_x - scroll_x;
        int32_t span =
            start_x - pager->snap_positions[start_page - 1u];

        if(span > 0 &&
           (int64_t)distance * 1000 >=
               (int64_t)span *
                   HOME_PAGER_RELEASE_THRESHOLD_PER_MILLE) {
            return start_page - 1u;
        }
    }
    return start_page;
}

static void home_pager_align_to_page(home_pager_t *pager, uint8_t page_index)
{
    int32_t scroll_x;

    if(page_index >= pager->physical_page_count) return;
    if(!pager->geometry_valid && !home_pager_refresh_geometry(pager)) return;
    scroll_x = lv_obj_get_scroll_x(pager->carousel);
    if(scroll_x == pager->snap_positions[page_index]) return;

    pager->correcting_snap = true;
    (void)home_pager_scroll_to_page(pager, page_index, LV_ANIM_OFF);
    pager->correcting_snap = false;
}

static bool home_pager_scroll_to_page(
    home_pager_t *pager, uint8_t page_index, lv_anim_enable_t animation)
{
    bool previous_suppress;

    if(page_index >= pager->physical_page_count) return false;
    if(!pager->geometry_valid && !home_pager_refresh_geometry(pager)) {
        return false;
    }
    previous_suppress = pager->suppress_scroll_end;
    pager->suppress_scroll_end = true;
    lv_obj_scroll_to_x(
        pager->carousel, pager->snap_positions[page_index], animation);
    pager->suppress_scroll_end = previous_suppress;
    return lv_obj_is_scrolling(pager->carousel);
}

static void home_pager_commit_page(
    home_pager_t *pager, uint8_t page_index, bool emit_event)
{
    bool changed;

    if(page_index >= pager->page_count) return;
    changed = pager->current_page != page_index;
    pager->current_page = page_index;
    home_pager_create_page_content(
        pager, home_pager_physical_page(pager, page_index));
    home_pager_update_indicators(pager);
    if(emit_event && changed) {
        home_pager_emit(
            pager, HOME_PAGER_EVENT_PAGE_CHANGED, page_index, 0u);
    }
}

static void home_pager_on_scroll_begin(lv_event_t *event)
{
    home_pager_t *pager = lv_event_get_user_data(event);
    lv_anim_t *animation = lv_event_get_scroll_anim(event);

    if(pager->suppress_scroll_end || pager->correcting_snap) return;
    if(animation == NULL) {
        if(pager->programmatic_scroll) {
            pager->programmatic_scroll = false;
            pager->target_physical_page = HOME_PAGER_NO_PAGE;
        }
        pager->gesture_start_physical_page = home_pager_nearest_page(pager);
    }
    if(animation != NULL && pager->release_page_pending) {
        if(!pager->geometry_valid && !home_pager_refresh_geometry(pager)) return;
        if(pager->release_physical_page >= pager->physical_page_count) return;
        lv_anim_set_values(
            animation,
            -lv_obj_get_scroll_x(pager->carousel),
            -pager->snap_positions[pager->release_physical_page]);
        pager->target_physical_page = pager->release_physical_page;
        pager->programmatic_scroll = true;
        pager->release_page_pending = false;
    }
    pager->scroll_active = true;
    if(pager->gesture_start_physical_page >= pager->physical_page_count) {
        pager->gesture_start_physical_page = home_pager_nearest_page(pager);
    }
    home_pager_prepare_neighbors(pager, home_pager_nearest_page(pager));
}

static void home_pager_on_scroll(lv_event_t *event)
{
    home_pager_t *pager = lv_event_get_user_data(event);

    home_pager_update_indicators(pager);
}

static void home_pager_on_scroll_throw_begin(lv_event_t *event)
{
    home_pager_t *pager = lv_event_get_user_data(event);
    lv_indev_t *indev = lv_event_get_param(event);
    lv_point_t vector;

    if(pager->programmatic_scroll || !pager->scroll_active) return;
    if(indev == NULL) indev = lv_indev_active();
    lv_indev_get_vect(indev, &vector);
    if(!pager->geometry_valid && !home_pager_refresh_geometry(pager)) return;
    pager->release_physical_page = home_pager_release_page(pager, vector.x);
    pager->release_page_pending = true;
    home_pager_prepare_neighbors(pager, pager->release_physical_page);
}

static void home_pager_on_scroll_end(lv_event_t *event)
{
    home_pager_t *pager = lv_event_get_user_data(event);
    lv_indev_t *indev = lv_indev_active();
    uint8_t page_index;

    if(pager->suppress_scroll_end || pager->correcting_snap) return;
    if(indev != NULL && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
        return;
    }
    if(!pager->geometry_valid && !home_pager_refresh_geometry(pager)) return;
    if(pager->release_page_pending) {
        uint8_t release_page = pager->release_physical_page;

        pager->release_page_pending = false;
        if(release_page >= pager->physical_page_count) {
            release_page = home_pager_nearest_page(pager);
        }
        if(lv_obj_get_scroll_x(pager->carousel) !=
           pager->snap_positions[release_page]) {
            pager->target_physical_page = release_page;
            pager->programmatic_scroll = true;
            home_pager_prepare_neighbors(pager, release_page);
            if(home_pager_scroll_to_page(pager, release_page, LV_ANIM_ON)) {
                return;
            }
        }
    }
    if(pager->programmatic_scroll &&
       pager->target_physical_page != HOME_PAGER_NO_PAGE &&
       lv_obj_get_scroll_x(pager->carousel) !=
           pager->snap_positions[pager->target_physical_page]) {
        home_pager_prepare_neighbors(pager, pager->target_physical_page);
        if(home_pager_scroll_to_page(
               pager, pager->target_physical_page, LV_ANIM_ON)) {
            return;
        }
    }
    page_index = home_pager_nearest_page(pager);
    if(pager->programmatic_scroll &&
       pager->target_physical_page != HOME_PAGER_NO_PAGE &&
       lv_obj_get_scroll_x(pager->carousel) ==
           pager->snap_positions[pager->target_physical_page]) {
        page_index = pager->target_physical_page;
    }
    if(home_pager_is_sentinel_page(pager, page_index)) {
        uint8_t logical_page =
            home_pager_logical_page(pager, page_index);
        uint8_t real_page = home_pager_physical_page(pager, logical_page);

        pager->correcting_snap = true;
        (void)home_pager_scroll_to_page(pager, real_page, LV_ANIM_OFF);
        pager->correcting_snap = false;
        page_index = real_page;
    }
    home_pager_align_to_page(pager, page_index);
    pager->scroll_active = false;
    pager->programmatic_scroll = false;
    pager->target_physical_page = HOME_PAGER_NO_PAGE;
    home_pager_commit_page(
        pager, home_pager_logical_page(pager, page_index), true);
}

static void home_pager_on_size_changed(lv_event_t *event)
{
    home_pager_t *pager = lv_event_get_user_data(event);

    pager->geometry_valid = false;
    if(pager->carousel == NULL || pager->indicator_track == NULL ||
       pager->scroll_active || pager->programmatic_scroll ||
       pager->geometry_refreshing) {
        return;
    }
    if(!home_pager_refresh_geometry(pager)) return;
    home_pager_align_to_page(
        pager, home_pager_physical_page(pager, pager->current_page));
    home_pager_update_indicators(pager);
}

static bool home_pager_copy_config(
    home_pager_t *pager, const home_pager_config_t *config)
{
    if(config == NULL || config->pages == NULL ||
       config->page_count == 0u ||
       config->page_count > HOME_PAGER_MAX_PAGES) {
        return false;
    }

    pager->page_count = config->page_count;
    pager->physical_page_count = config->page_count > 1u
                                     ? (uint8_t)(config->page_count + 2u)
                                     : config->page_count;
    pager->target_physical_page = HOME_PAGER_NO_PAGE;
    pager->current_page = config->initial_page < config->page_count
                              ? config->initial_page
                              : (uint8_t)(config->page_count - 1u);
    pager->lazy_load = config->lazy_load;
    pager->top_inset = config->top_inset;
    pager->symbol_font = config->symbol_font;
    pager->title_font = config->title_font;
    pager->caption_font = config->caption_font;
    pager->on_event = config->on_event;
    pager->user_ctx = config->user_ctx;

    for(uint8_t page_index = 0u;
        page_index < pager->page_count;
        ++page_index) {
        const home_pager_page_t *source_page = &config->pages[page_index];

        if(source_page->items == NULL || source_page->item_count == 0u ||
           source_page->item_count > HOME_PAGER_MAX_ITEMS_PER_PAGE) {
            return false;
        }
        pager->pages[page_index].item_count = source_page->item_count;
        pager->pages[page_index].items = pager->items[page_index];
        memcpy(
            pager->items[page_index],
            source_page->items,
            (size_t)source_page->item_count * sizeof(home_pager_item_t));
    }
    return true;
}

static bool home_pager_build(home_pager_t *pager, lv_obj_t *parent)
{
    lv_obj_t *indicator_container;

    pager->root = lv_obj_create(parent);
    if(pager->root == NULL) return false;
    home_pager_make_plain(pager->root);
    lv_obj_set_size(pager->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(pager->root, lv_color_hex(0x090B0F), 0);
    lv_obj_set_style_bg_opa(pager->root, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(
        pager->root, home_pager_on_root_delete, LV_EVENT_DELETE, pager);

    pager->carousel = lv_obj_create(pager->root);
    if(pager->carousel == NULL) return false;
    home_pager_make_plain(pager->carousel);
    lv_obj_add_flag(pager->carousel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pager->carousel, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_flag(pager->carousel, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_size(pager->carousel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_base_dir(pager->carousel, LV_BASE_DIR_LTR, 0);
    lv_obj_set_scroll_dir(pager->carousel, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(pager->carousel, LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(pager->carousel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(pager->carousel, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(pager->carousel, 0, 0);
    lv_obj_set_style_pad_gap(pager->carousel, 0, 0);
    lv_obj_add_event_cb(
        pager->carousel,
        home_pager_on_scroll_begin,
        LV_EVENT_SCROLL_BEGIN,
        pager);
    lv_obj_add_event_cb(
        pager->carousel,
        home_pager_on_scroll_throw_begin,
        LV_EVENT_SCROLL_THROW_BEGIN,
        pager);
    lv_obj_add_event_cb(
        pager->carousel, home_pager_on_scroll, LV_EVENT_SCROLL, pager);
    lv_obj_add_event_cb(
        pager->carousel, home_pager_on_scroll_end, LV_EVENT_SCROLL_END, pager);
    lv_obj_add_event_cb(
        pager->carousel,
        home_pager_on_size_changed,
        LV_EVENT_SIZE_CHANGED,
        pager);
    lv_obj_add_event_cb(
        pager->carousel,
        home_pager_on_size_changed,
        LV_EVENT_LAYOUT_CHANGED,
        pager);

    for(uint8_t physical_page_index = 0u;
        physical_page_index < pager->physical_page_count;
        ++physical_page_index) {
        lv_obj_t *page = lv_obj_create(pager->carousel);

        if(page == NULL) return false;
        pager->page_objects[physical_page_index] = page;
        home_pager_make_plain(page);
        lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
        lv_obj_add_flag(page, LV_OBJ_FLAG_SNAPPABLE);
        lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_top(page, (int32_t)pager->top_inset + 12, 0);
        lv_obj_set_style_pad_bottom(page, 52, 0);
        lv_obj_set_style_pad_left(page, 28, 0);
        lv_obj_set_style_pad_right(page, 28, 0);
        lv_obj_set_style_pad_row(page, 14, 0);
        lv_obj_set_style_pad_column(page, 14, 0);
    }

    indicator_container = lv_obj_create(pager->root);
    if(indicator_container == NULL) return false;
    home_pager_make_plain(indicator_container);
    lv_obj_set_size(indicator_container, LV_PCT(100), 32);
    lv_obj_align(indicator_container, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_opa(indicator_container, LV_OPA_TRANSP, 0);

    pager->indicator_track = lv_obj_create(indicator_container);
    if(pager->indicator_track == NULL) return false;
    home_pager_make_plain(pager->indicator_track);
    lv_obj_set_size(
        pager->indicator_track,
        HOME_PAGER_INDICATOR_MAX_WIDTH +
            (int32_t)(pager->page_count - 1u) *
                HOME_PAGER_INDICATOR_PITCH,
        8);
    lv_obj_align(pager->indicator_track, LV_ALIGN_CENTER, 0, 0);

    for(uint8_t page_index = 0u;
        page_index < pager->page_count;
        ++page_index) {
        lv_obj_t *indicator = lv_obj_create(pager->indicator_track);

        if(indicator == NULL) return false;
        pager->indicators[page_index] = indicator;
        home_pager_make_plain(indicator);
        lv_obj_set_size(indicator, 8, 8);
        lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(indicator, lv_color_hex(0xFFFFFF), 0);
        home_pager_set_indicator_visual(
            pager,
            page_index,
            page_index == pager->current_page
                ? HOME_PAGER_INDICATOR_MAX_WIDTH
                : HOME_PAGER_INDICATOR_MIN_WIDTH,
            page_index == pager->current_page ? LV_OPA_80 : LV_OPA_40);
    }
    lv_obj_move_foreground(indicator_container);

    lv_obj_add_event_cb(
        pager->root,
        home_pager_on_size_changed,
        LV_EVENT_SIZE_CHANGED,
        pager);

    if(pager->lazy_load) {
        home_pager_create_page_content(
            pager, home_pager_physical_page(pager, pager->current_page));
    }
    else {
        for(uint8_t physical_page_index = 0u;
            physical_page_index < pager->physical_page_count;
            ++physical_page_index) {
            home_pager_create_page_content(pager, physical_page_index);
        }
    }
    home_pager_set_page(pager, pager->current_page, LV_ANIM_OFF);
    return true;
}

home_pager_t *home_pager_create(
    lv_obj_t *parent, const home_pager_config_t *config)
{
    home_pager_t *pager;

    if(parent == NULL) return NULL;
    pager = lv_malloc_zeroed(sizeof(*pager));
    if(pager == NULL) return NULL;
    if(!home_pager_copy_config(pager, config)) {
        lv_free(pager);
        return NULL;
    }
    if(!home_pager_build(pager, parent)) {
        if(pager->root != NULL) {
            lv_obj_delete(pager->root);
        }
        else {
            lv_free(pager);
        }
        return NULL;
    }
    return pager;
}

void home_pager_destroy(home_pager_t *pager)
{
    if(pager == NULL) return;
    if(pager->root != NULL) {
        lv_obj_delete(pager->root);
    }
}

lv_obj_t *home_pager_root(home_pager_t *pager)
{
    return pager != NULL ? pager->root : NULL;
}

void home_pager_set_page(
    home_pager_t *pager,
    uint8_t page_index,
    lv_anim_enable_t animation)
{
    uint8_t from_physical_page;
    uint8_t physical_page_index;

    if(pager == NULL) return;
    if(page_index >= pager->page_count) page_index = pager->page_count - 1u;
    if(!pager->geometry_valid && !home_pager_refresh_geometry(pager)) return;
    from_physical_page = home_pager_nearest_page(pager);
    physical_page_index = home_pager_physical_page(pager, page_index);
    home_pager_create_page_content(pager, physical_page_index);
    if(animation == LV_ANIM_ON) {
        home_pager_prepare_transition(
            pager, from_physical_page, physical_page_index);
    }
    pager->target_physical_page = physical_page_index;
    pager->programmatic_scroll = true;
    if(!home_pager_scroll_to_page(
           pager, physical_page_index, animation)) {
        pager->programmatic_scroll = false;
        pager->target_physical_page = HOME_PAGER_NO_PAGE;
        pager->scroll_active = false;
        home_pager_commit_page(pager, page_index, true);
    }
}

uint8_t home_pager_get_page(const home_pager_t *pager)
{
    return pager != NULL ? pager->current_page : 0u;
}

uint8_t home_pager_get_created_page_count(const home_pager_t *pager)
{
    bool logical_page_created[HOME_PAGER_MAX_PAGES] = {false};
    uint8_t count = 0u;

    if(pager == NULL) return 0u;
    for(uint8_t physical_page_index = 0u;
        physical_page_index < pager->physical_page_count;
        ++physical_page_index) {
        if(pager->page_created[physical_page_index]) {
            logical_page_created[home_pager_logical_page(
                pager, physical_page_index)] = true;
        }
    }
    for(uint8_t page_index = 0u;
        page_index < pager->page_count;
        ++page_index) {
        if(logical_page_created[page_index]) ++count;
    }
    return count;
}

void home_pager_set_lazy_load(home_pager_t *pager, bool enabled)
{
    if(pager == NULL || pager->lazy_load == enabled) return;
    pager->lazy_load = enabled;
    if(!enabled) {
        for(uint8_t physical_page_index = 0u;
            physical_page_index < pager->physical_page_count;
            ++physical_page_index) {
            home_pager_create_page_content(pager, physical_page_index);
        }
    }
}
