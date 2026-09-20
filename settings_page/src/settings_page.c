#include "settings_page/settings_page.h"

#include <stddef.h>
#include <string.h>

#define SETTINGS_PAGE_DEFAULT_HEADER_HEIGHT 64u
#define SETTINGS_PAGE_GRID_COLUMN_COUNT 3u
#define SETTINGS_PAGE_LIST_ROW_HEIGHT 72
#define SETTINGS_PAGE_SECTION_HEIGHT 28
#define SETTINGS_PAGE_PRODUCT_BACKGROUND_COLOR 0xEBF9FF
#define SETTINGS_PAGE_PRODUCT_TITLE_COLOR 0x963B02
#define SETTINGS_PAGE_PRODUCT_ACCENT_COLOR 0x20AAF0
#define SETTINGS_PAGE_PRODUCT_STORAGE_HEIGHT 480
#define SETTINGS_PAGE_PRODUCT_UPGRADE_BUTTON_COLOR 0xFFCA5E

typedef struct {
    struct settings_page *page;
    uint8_t row_index;
} settings_page_row_binding_t;

typedef struct {
    struct settings_page *page;
    uint8_t row_index;
    uint8_t option_index;
} settings_page_option_binding_t;

struct settings_page {
    lv_obj_t *root;
    lv_obj_t *header;
    lv_obj_t *title_label;
    lv_obj_t *back_button;
    lv_obj_t *content;
    lv_obj_t *background_image;
    lv_obj_t *title_background_image;
    lv_obj_t *row_objects[SETTINGS_PAGE_MAX_ROWS];
    lv_obj_t *value_labels[SETTINGS_PAGE_MAX_ROWS];
    lv_obj_t *switch_objects[SETTINGS_PAGE_MAX_ROWS];
    lv_obj_t *slider_objects[SETTINGS_PAGE_MAX_ROWS];
    lv_obj_t *choice_objects[SETTINGS_PAGE_MAX_ROWS];
    lv_obj_t *choice_option_objects
        [SETTINGS_PAGE_MAX_ROWS][SETTINGS_PAGE_MAX_OPTIONS];
    lv_obj_t *wifi_connected_objects[SETTINGS_PAGE_MAX_ROWS];
    lv_obj_t *wifi_encrypted_objects[SETTINGS_PAGE_MAX_ROWS];
    lv_obj_t *wifi_signal_objects[SETTINGS_PAGE_MAX_ROWS];
    lv_obj_t *wifi_status_labels[SETTINGS_PAGE_MAX_ROWS];
    lv_obj_t *wifi_loading_object;
    lv_obj_t *decrement_button;
    lv_obj_t *increment_button;
    settings_page_row_binding_t bindings[SETTINGS_PAGE_MAX_ROWS];
    settings_page_option_binding_t option_bindings
        [SETTINGS_PAGE_MAX_ROWS][SETTINGS_PAGE_MAX_OPTIONS];
    settings_page_row_t rows[SETTINGS_PAGE_MAX_ROWS];
    int32_t grid_columns[SETTINGS_PAGE_GRID_COLUMN_COUNT + 1u];
    int32_t grid_rows[SETTINGS_PAGE_MAX_ROWS + 1u];
    settings_page_theme_t theme;
    settings_page_layout_t layout;
    settings_page_event_fn on_event;
    void *user_ctx;
    const char *title;
    const lv_font_t *symbol_font;
    const lv_font_t *title_font;
    const lv_font_t *overview_font;
    const lv_font_t *emphasis_font;
    const lv_font_t *body_font;
    const lv_font_t *section_font;
    uint8_t row_count;
    uint8_t click_counts[SETTINGS_PAGE_MAX_ROWS];
    uint32_t click_times[SETTINGS_PAGE_MAX_ROWS];
    bool suppress_click[SETTINGS_PAGE_MAX_ROWS];
    bool wifi_current[SETTINGS_PAGE_MAX_ROWS];
    bool wifi_loading;
    bool wifi_enabled;
    uint16_t header_height;
    bool show_back;
    bool updating;
    settings_page_style_t style;
    settings_page_variant_t variant;
    settings_page_assets_t assets;
};

static void settings_page_on_key(lv_event_t *event);
static void settings_page_on_row_press_lost(lv_event_t *event);

static const lv_font_t *settings_page_font_or_default(const lv_font_t *font)
{
    return font != NULL ? font : LV_FONT_DEFAULT;
}

static void settings_page_set_default_theme(settings_page_theme_t *theme)
{
    theme->background = lv_color_hex(0x090B0F);
    theme->panel = lv_color_hex(0x232323);
    theme->row = lv_color_hex(0xFFF4DE);
    theme->accent = lv_color_hex(0x2EE4F9);
    theme->text = lv_color_hex(0x17212B);
    theme->muted_text = lv_color_hex(0x66717D);
}

static bool settings_page_is_product(const settings_page_t *page)
{
    return page->style == SETTINGS_PAGE_STYLE_UNIGUI;
}

static void settings_page_set_product_theme(settings_page_theme_t *theme)
{
    theme->background = lv_color_hex(SETTINGS_PAGE_PRODUCT_BACKGROUND_COLOR);
    theme->panel = lv_color_hex(0xFFFFFF);
    theme->row = lv_color_hex(0xFFFFFF);
    theme->accent = lv_color_hex(SETTINGS_PAGE_PRODUCT_ACCENT_COLOR);
    theme->text = lv_color_hex(0x222222);
    theme->muted_text = lv_color_hex(0x555555);
}

static lv_color_t settings_page_accent_color(settings_page_accent_t accent)
{
    switch(accent) {
        case SETTINGS_PAGE_ACCENT_BLUE:
            return lv_color_hex(0x3388F4);
        case SETTINGS_PAGE_ACCENT_PINK:
            return lv_color_hex(0xF866A5);
        case SETTINGS_PAGE_ACCENT_PURPLE:
            return lv_color_hex(0x8869E8);
        case SETTINGS_PAGE_ACCENT_GREEN:
            return lv_color_hex(0x55D66B);
        case SETTINGS_PAGE_ACCENT_ORANGE:
            return lv_color_hex(0xFF8A34);
        case SETTINGS_PAGE_ACCENT_YELLOW:
        default:
            return lv_color_hex(0xFFD72E);
    }
}

static lv_color_t settings_page_tile_foreground(settings_page_accent_t accent)
{
    if(accent == SETTINGS_PAGE_ACCENT_YELLOW ||
       accent == SETTINGS_PAGE_ACCENT_GREEN ||
       accent == SETTINGS_PAGE_ACCENT_ORANGE) {
        return lv_color_hex(0x17212B);
    }
    return lv_color_hex(0xFFFFFF);
}

static void settings_page_make_plain(lv_obj_t *object)
{
    lv_obj_remove_style_all(object);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

static void settings_page_add_key_handler(
    settings_page_t *page, lv_obj_t *object)
{
    if(object != NULL) {
        lv_obj_add_event_cb(object, settings_page_on_key, LV_EVENT_KEY, page);
    }
}

static uint16_t settings_page_asset_or_default(
    uint16_t value, uint16_t fallback)
{
    return value != 0u ? value : fallback;
}

static lv_obj_t *settings_page_create_image(
    lv_obj_t *parent, const void *source, uint16_t width, uint16_t height)
{
    lv_obj_t *image;

    if(source == NULL) return NULL;
    image = lv_image_create(parent);
    if(image == NULL) return NULL;
    lv_image_set_src(image, source);
    if(width != 0u && height != 0u) lv_obj_set_size(image, width, height);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
    return image;
}

static const void *settings_page_asset_background(
    const settings_page_t *page)
{
    return page->assets.background_src;
}

static const void *settings_page_asset_title_background(
    const settings_page_t *page)
{
    return page->assets.title_background_src;
}

static const void *settings_page_asset_back(const settings_page_t *page)
{
    return page->assets.back_src;
}

static const void *settings_page_asset_arrow(const settings_page_t *page)
{
    return page->assets.arrow_src;
}

static const void *settings_page_asset_wifi_connected(
    const settings_page_t *page)
{
    return page->assets.wifi_connected_src;
}

static const void *settings_page_asset_wifi_encrypted(
    const settings_page_t *page)
{
    return page->assets.wifi_encrypted_src;
}

static const void *settings_page_asset_wifi_signal(
    const settings_page_t *page, const settings_page_row_t *row)
{
    if(row->image_src != NULL && row->wifi_signal_strength == 0) {
        return row->image_src;
    }
    if(row->wifi_signal_strength >= -70) {
        return page->assets.wifi_signal_strong_src != NULL
                   ? page->assets.wifi_signal_strong_src
                   : row->image_src;
    }
    if(row->wifi_signal_strength >= -90) {
        return page->assets.wifi_signal_medium_src != NULL
                   ? page->assets.wifi_signal_medium_src
                   : row->image_src;
    }
    return page->assets.wifi_signal_weak_src != NULL
               ? page->assets.wifi_signal_weak_src
               : row->image_src;
}

static int32_t settings_page_clamp_value(
    const settings_page_row_t *row, int32_t value)
{
    if(value < row->min_value) return row->min_value;
    if(value > row->max_value) return row->max_value;
    return value;
}

static int32_t settings_page_snap_value(
    const settings_page_row_t *row, int32_t value)
{
    int32_t clamped = settings_page_clamp_value(row, value);
    int64_t offset;
    int64_t snapped;

    if(row->step <= 1) return clamped;
    offset = (int64_t)clamped - row->min_value;
    snapped = (int64_t)row->min_value +
              ((offset + row->step / 2) / row->step) * row->step;
    if(snapped > row->max_value) return row->max_value;
    return (int32_t)snapped;
}

static int8_t settings_page_find_row(
    const settings_page_t *page, uint16_t row_id)
{
    for(uint8_t row_index = 0u; row_index < page->row_count; ++row_index) {
        if(page->rows[row_index].id == row_id) return (int8_t)row_index;
    }
    return -1;
}

static void settings_page_sync_wifi_enabled(settings_page_t *page)
{
    page->wifi_enabled = true;
    if(page->variant != SETTINGS_PAGE_VARIANT_UNIGUI_WIFI) return;
    for(uint8_t row_index = 0u; row_index < page->row_count; ++row_index) {
        if(page->rows[row_index].kind == SETTINGS_PAGE_ROW_SWITCH) {
            page->wifi_enabled = page->rows[row_index].checked;
            return;
        }
    }
}

static void settings_page_emit_click(
    settings_page_t *page,
    settings_page_event_kind_t kind,
    uint8_t row_index,
    int32_t value,
    uint8_t option_index,
    uint8_t click_count)
{
    settings_page_event_t event;

    if(page->on_event == NULL) return;
    event.kind = kind;
    event.row_id = row_index < page->row_count
                       ? page->rows[row_index].id
                       : 0u;
    event.value = value;
    event.option_index = option_index;
    event.click_count = click_count != 0u ? click_count : 1u;
    page->on_event(page->user_ctx, &event);
}

static void settings_page_emit(
    settings_page_t *page,
    settings_page_event_kind_t kind,
    uint8_t row_index,
    int32_t value,
    uint8_t option_index)
{
    settings_page_emit_click(
        page, kind, row_index, value, option_index, 1u);
}

static bool settings_page_accept_activation(
    settings_page_t *page, uint8_t row_index, uint8_t *click_count)
{
    settings_page_row_t *row = &page->rows[row_index];
    uint8_t required = row->activation_clicks != 0u
                           ? row->activation_clicks
                           : 1u;
    uint32_t window = row->activation_window_ms != 0u
                          ? row->activation_window_ms
                          : 2000u;
    uint32_t now;

    if(required <= 1u) {
        *click_count = 1u;
        return true;
    }
    now = lv_tick_get();
    if(page->click_counts[row_index] == 0u ||
       lv_tick_elaps(page->click_times[row_index]) > window) {
        page->click_counts[row_index] = 1u;
    }
    else if(page->click_counts[row_index] < required) {
        ++page->click_counts[row_index];
    }
    page->click_times[row_index] = now;
    if(page->click_counts[row_index] < required) return false;
    *click_count = page->click_counts[row_index];
    page->click_counts[row_index] = 0u;
    return true;
}

static bool settings_page_is_row_disabled(
    const settings_page_t *page, uint8_t row_index)
{
    const settings_page_row_t *row = &page->rows[row_index];

    return row->disabled ||
           (page->variant == SETTINGS_PAGE_VARIANT_UNIGUI_WIFI &&
            row->kind != SETTINGS_PAGE_ROW_SWITCH && !page->wifi_enabled);
}

static void settings_page_set_value_label(
    settings_page_t *page, uint8_t row_index)
{
    const settings_page_row_t *row = &page->rows[row_index];
    lv_obj_t *label = page->value_labels[row_index];

    if(label == NULL) return;
    if(row->value_text != NULL && row->value_text[0] != '\0') {
        lv_label_set_text(label, row->value_text);
    }
    else if(row->kind == SETTINGS_PAGE_ROW_SLIDER) {
        lv_label_set_text_fmt(label, "%ld", (long)row->value);
    }
    else if(page->layout == SETTINGS_PAGE_LAYOUT_GRID &&
            row->subtitle != NULL) {
        lv_label_set_text(label, row->subtitle);
    }
    else {
        lv_label_set_text(label, "");
    }
    if(page->variant == SETTINGS_PAGE_VARIANT_UNIGUI_ABOUT &&
       (row->kind == SETTINGS_PAGE_ROW_NAVIGATION ||
        row->kind == SETTINGS_PAGE_ROW_ACTION)) {
        lv_obj_set_style_bg_opa(
            label,
            row->value_text != NULL && row->value_text[0] != '\0'
                ? LV_OPA_50
                : LV_OPA_TRANSP,
            0);
    }
}

static void settings_page_set_hidden(lv_obj_t *object, bool hidden)
{
    if(object == NULL) return;
    lv_obj_set_flag(object, LV_OBJ_FLAG_HIDDEN, hidden);
}

static void settings_page_update_wifi_visual(
    settings_page_t *page, uint8_t row_index)
{
    const settings_page_row_t *row = &page->rows[row_index];
    const void *signal_source = settings_page_asset_wifi_signal(page, row);
    bool legacy_current =
        row->wifi_status == SETTINGS_PAGE_WIFI_STATUS_NONE &&
        row->wifi_signal_strength == 0 && !row->wifi_encrypted &&
        !row->wifi_remembered;
    bool show_connected =
        row->wifi_status == SETTINGS_PAGE_WIFI_STATUS_CONNECTED ||
        (page->wifi_current[row_index] && legacy_current);
    const char *status_text = "";

    if(row->wifi_status == SETTINGS_PAGE_WIFI_STATUS_CONNECTING) {
        status_text = "连接中...";
    }
    else if(row->wifi_status == SETTINGS_PAGE_WIFI_STATUS_ERROR) {
        status_text = "连接失败";
    }
    settings_page_set_hidden(
        page->wifi_connected_objects[row_index], !show_connected);
    settings_page_set_hidden(
        page->wifi_encrypted_objects[row_index],
        !row->wifi_encrypted || row->wifi_remembered);
    if(page->wifi_status_labels[row_index] != NULL) {
        lv_label_set_text(page->wifi_status_labels[row_index], status_text);
        settings_page_set_hidden(
            page->wifi_status_labels[row_index], status_text[0] == '\0');
    }
    if(signal_source != NULL && page->wifi_signal_objects[row_index] != NULL &&
       lv_obj_check_type(
           page->wifi_signal_objects[row_index], &lv_image_class)) {
        lv_image_set_src(page->wifi_signal_objects[row_index], signal_source);
    }
}

static void settings_page_update_row_visual(
    settings_page_t *page, uint8_t row_index)
{
    const settings_page_row_t *row;
    bool disabled;

    if(row_index >= page->row_count) return;
    row = &page->rows[row_index];
    disabled = row->disabled;
    if(page->variant == SETTINGS_PAGE_VARIANT_UNIGUI_WIFI &&
       row->kind != SETTINGS_PAGE_ROW_SWITCH && !page->wifi_enabled) {
        disabled = true;
    }
    if(page->row_objects[row_index] != NULL) {
        lv_obj_set_state(
            page->row_objects[row_index], LV_STATE_DISABLED, disabled);
    }
    if(page->switch_objects[row_index] != NULL) {
        page->updating = true;
        lv_obj_set_state(
            page->switch_objects[row_index], LV_STATE_CHECKED, row->checked);
        page->updating = false;
        lv_obj_set_state(
            page->switch_objects[row_index], LV_STATE_DISABLED, disabled);
    }
    if(page->slider_objects[row_index] != NULL) {
        page->updating = true;
        lv_slider_set_value(
            page->slider_objects[row_index], row->value, LV_ANIM_OFF);
        page->updating = false;
        lv_obj_set_state(
            page->slider_objects[row_index], LV_STATE_DISABLED, disabled);
        if(page->decrement_button != NULL) {
            lv_obj_set_state(
                page->decrement_button, LV_STATE_DISABLED, disabled);
        }
        if(page->increment_button != NULL) {
            lv_obj_set_state(
                page->increment_button, LV_STATE_DISABLED, disabled);
        }
    }
    if(page->variant == SETTINGS_PAGE_VARIANT_UNIGUI_STORAGE &&
       row_index == 0u && page->row_objects[row_index] != NULL) {
        int32_t percent = row->value;

        if(percent < 0) percent = 0;
        if(percent > 100) percent = 100;
        lv_bar_set_value(
            page->row_objects[row_index], percent, LV_ANIM_OFF);
    }
    if(page->choice_objects[row_index] != NULL) {
        page->updating = true;
        lv_dropdown_set_selected(
            page->choice_objects[row_index], row->selected_option);
        page->updating = false;
        lv_obj_set_state(
            page->choice_objects[row_index], LV_STATE_DISABLED, disabled);
    }
    for(uint8_t option_index = 0u;
        option_index < SETTINGS_PAGE_MAX_OPTIONS;
        ++option_index) {
        lv_obj_t *option = page->choice_option_objects[row_index][option_index];

        if(option == NULL) continue;
        page->updating = true;
        lv_obj_set_state(
            option,
            LV_STATE_CHECKED,
            option_index == row->selected_option);
        page->updating = false;
        lv_obj_set_state(option, LV_STATE_DISABLED, disabled);
    }
    settings_page_set_value_label(page, row_index);
    if(page->variant == SETTINGS_PAGE_VARIANT_UNIGUI_WIFI) {
        settings_page_update_wifi_visual(page, row_index);
    }
}

static void settings_page_on_back_clicked(lv_event_t *event)
{
    settings_page_t *page = lv_event_get_user_data(event);

    if(page == NULL) return;
    settings_page_emit(
        page, SETTINGS_PAGE_EVENT_BACK, page->row_count, 0, 0u);
}

static void settings_page_on_row_clicked(lv_event_t *event)
{
    settings_page_row_binding_t *binding = lv_event_get_user_data(event);
    settings_page_t *page;
    settings_page_row_t *row;
    uint8_t click_count;

    if(binding == NULL || binding->page == NULL) return;
    page = binding->page;
    if(binding->row_index >= page->row_count) return;
    row = &page->rows[binding->row_index];

    if(page->updating || settings_page_is_row_disabled(page, binding->row_index)) {
        return;
    }
    if(page->suppress_click[binding->row_index]) {
        page->suppress_click[binding->row_index] = false;
        return;
    }
    if(row->kind == SETTINGS_PAGE_ROW_SWITCH) {
        row->checked = !row->checked;
        settings_page_update_row_visual(page, binding->row_index);
        settings_page_emit(
            page,
            SETTINGS_PAGE_EVENT_SWITCH_CHANGED,
            binding->row_index,
            row->checked ? 1 : 0,
            0u);
    }
    else if(row->kind == SETTINGS_PAGE_ROW_CHOICE) {
        if(page->choice_objects[binding->row_index] != NULL) {
            lv_dropdown_open(page->choice_objects[binding->row_index]);
        }
    }
    else if(row->kind == SETTINGS_PAGE_ROW_NAVIGATION ||
            row->kind == SETTINGS_PAGE_ROW_ACTION ||
            row->kind == SETTINGS_PAGE_ROW_VALUE) {
        if(!settings_page_accept_activation(
               page, binding->row_index, &click_count)) {
            return;
        }
        settings_page_emit_click(
            page,
            SETTINGS_PAGE_EVENT_ROW_ACTIVATED,
            binding->row_index,
            row->value,
            row->selected_option,
            click_count);
    }
}

static void settings_page_on_row_long_pressed(lv_event_t *event)
{
    settings_page_row_binding_t *binding = lv_event_get_user_data(event);
    settings_page_t *page;
    settings_page_row_t *row;

    if(binding == NULL || binding->page == NULL) return;
    page = binding->page;
    if(binding->row_index >= page->row_count) return;
    row = &page->rows[binding->row_index];

    if(page->updating ||
       settings_page_is_row_disabled(page, binding->row_index) ||
       (row->kind != SETTINGS_PAGE_ROW_NAVIGATION &&
        row->kind != SETTINGS_PAGE_ROW_ACTION &&
        row->kind != SETTINGS_PAGE_ROW_VALUE)) {
        return;
    }
    page->click_counts[binding->row_index] = 0u;
    page->suppress_click[binding->row_index] = true;
    settings_page_emit(
        page,
        SETTINGS_PAGE_EVENT_ROW_LONG_PRESSED,
        binding->row_index,
        row->value,
        row->selected_option);
}

static void settings_page_on_row_press_lost(lv_event_t *event)
{
    settings_page_row_binding_t *binding = lv_event_get_user_data(event);

    if(binding == NULL || binding->page == NULL ||
       binding->row_index >= binding->page->row_count) {
        return;
    }
    binding->page->suppress_click[binding->row_index] = false;
    binding->page->click_counts[binding->row_index] = 0u;
}

static void settings_page_on_slider_changed(lv_event_t *event)
{
    settings_page_row_binding_t *binding = lv_event_get_user_data(event);
    settings_page_t *page;
    settings_page_row_t *row;

    if(binding == NULL || binding->page == NULL) return;
    page = binding->page;
    if(binding->row_index >= page->row_count ||
       page->slider_objects[binding->row_index] == NULL) {
        return;
    }
    row = &page->rows[binding->row_index];

    if(page->updating || settings_page_is_row_disabled(page, binding->row_index)) {
        return;
    }
    row->value = lv_slider_get_value(page->slider_objects[binding->row_index]);
    if(row->step > 1) {
        row->value = settings_page_snap_value(row, row->value);
        page->updating = true;
        lv_slider_set_value(
            page->slider_objects[binding->row_index], row->value, LV_ANIM_OFF);
        page->updating = false;
    }
    settings_page_set_value_label(page, binding->row_index);
    settings_page_emit(
        page,
        SETTINGS_PAGE_EVENT_SLIDER_CHANGED,
        binding->row_index,
        row->value,
        0u);
}

static void settings_page_on_slider_released(lv_event_t *event)
{
    settings_page_row_binding_t *binding = lv_event_get_user_data(event);
    settings_page_t *page;
    settings_page_row_t *row;

    if(binding == NULL || binding->page == NULL) return;
    page = binding->page;
    if(binding->row_index >= page->row_count) return;
    row = &page->rows[binding->row_index];

    if(page->updating || settings_page_is_row_disabled(page, binding->row_index)) {
        return;
    }
    settings_page_emit(
        page,
        SETTINGS_PAGE_EVENT_SLIDER_RELEASED,
        binding->row_index,
        row->value,
        0u);
}

static void settings_page_on_switch_changed(lv_event_t *event)
{
    settings_page_row_binding_t *binding = lv_event_get_user_data(event);
    settings_page_t *page;
    settings_page_row_t *row;

    if(binding == NULL || binding->page == NULL) return;
    page = binding->page;
    if(binding->row_index >= page->row_count ||
       page->switch_objects[binding->row_index] == NULL) {
        return;
    }
    row = &page->rows[binding->row_index];

    if(page->updating || settings_page_is_row_disabled(page, binding->row_index)) {
        return;
    }
    row->checked = lv_obj_has_state(
        page->switch_objects[binding->row_index], LV_STATE_CHECKED);
    if(page->variant == SETTINGS_PAGE_VARIANT_UNIGUI_WIFI) {
        page->wifi_enabled = row->checked;
        for(uint8_t row_index = 0u; row_index < page->row_count; ++row_index) {
            settings_page_update_row_visual(page, row_index);
        }
    }
    settings_page_emit(
        page,
        SETTINGS_PAGE_EVENT_SWITCH_CHANGED,
        binding->row_index,
        row->checked ? 1 : 0,
        0u);
}

static void settings_page_on_choice_changed(lv_event_t *event)
{
    settings_page_row_binding_t *binding = lv_event_get_user_data(event);
    settings_page_t *page;
    settings_page_row_t *row;

    if(binding == NULL || binding->page == NULL) return;
    page = binding->page;
    if(binding->row_index >= page->row_count ||
       page->choice_objects[binding->row_index] == NULL) {
        return;
    }
    row = &page->rows[binding->row_index];

    if(page->updating || settings_page_is_row_disabled(page, binding->row_index)) {
        return;
    }
    row->selected_option = (uint8_t)lv_dropdown_get_selected(
        page->choice_objects[binding->row_index]);
    settings_page_emit(
        page,
        SETTINGS_PAGE_EVENT_CHOICE_CHANGED,
        binding->row_index,
        row->selected_option,
        row->selected_option);
}

static void settings_page_on_choice_option_clicked(lv_event_t *event)
{
    settings_page_option_binding_t *binding = lv_event_get_user_data(event);
    settings_page_t *page;
    settings_page_row_t *row;

    if(binding == NULL || binding->page == NULL) return;
    page = binding->page;
    if(binding->row_index >= page->row_count) return;
    row = &page->rows[binding->row_index];

    if(page->updating ||
       settings_page_is_row_disabled(page, binding->row_index) ||
       binding->option_index >= row->option_count) {
        return;
    }
    row->selected_option = binding->option_index;
    settings_page_update_row_visual(page, binding->row_index);
    settings_page_emit(
        page,
        SETTINGS_PAGE_EVENT_CHOICE_CHANGED,
        binding->row_index,
        row->selected_option,
        row->selected_option);
}

static void settings_page_on_step_clicked(lv_event_t *event)
{
    settings_page_row_binding_t *binding = lv_event_get_user_data(event);
    settings_page_t *page;
    settings_page_row_t *row;
    lv_obj_t *target = lv_event_get_target(event);
    int32_t delta;

    if(binding == NULL || binding->page == NULL) return;
    page = binding->page;
    if(binding->row_index >= page->row_count) return;
    row = &page->rows[binding->row_index];
    if(page->updating ||
       settings_page_is_row_disabled(page, binding->row_index) ||
       page->slider_objects[binding->row_index] == NULL) {
        return;
    }
    if(page->variant == SETTINGS_PAGE_VARIANT_UNIGUI_BRIGHTNESS) {
        delta = target == page->increment_button ? 20 : -20;
    }
    else {
        delta = target == page->increment_button
                    ? page->assets.increment_step
                    : -page->assets.decrement_step;
    }
    if(delta == 0) delta = target == page->increment_button ? 10 : -10;
    row->value = settings_page_snap_value(row, row->value + delta);
    settings_page_update_row_visual(page, binding->row_index);
    settings_page_emit(
        page,
        SETTINGS_PAGE_EVENT_SLIDER_CHANGED,
        binding->row_index,
        row->value,
        0u);
}

static void settings_page_style_body_label(
    settings_page_t *page, lv_obj_t *label, bool muted)
{
    lv_obj_set_style_text_color(
        label, muted ? page->theme.muted_text : page->theme.text, 0);
    lv_obj_set_style_text_font(
        label, settings_page_font_or_default(page->body_font), 0);
}

static void settings_page_add_row_text(
    settings_page_t *page,
    lv_obj_t *row_object,
    const settings_page_row_t *row,
    bool grid)
{
    lv_obj_t *icon;
    lv_obj_t *title = lv_label_create(row_object);

    if(!grid && row->image_src != NULL) {
        icon = settings_page_create_image(row_object, row->image_src, 36u, 36u);
    }
    else {
        icon = lv_label_create(row_object);
        lv_label_set_text(
            icon, row->symbol != NULL ? row->symbol : LV_SYMBOL_SETTINGS);
        lv_obj_set_style_text_font(
            icon, settings_page_font_or_default(page->symbol_font), 0);
        lv_obj_set_style_text_color(
            icon,
            grid ? settings_page_tile_foreground(row->accent) : page->theme.accent,
            0);
    }

    lv_label_set_text(title, row->title != NULL ? row->title : "");
    settings_page_style_body_label(page, title, false);
    if(grid) {
        lv_obj_set_style_text_color(
            title, settings_page_tile_foreground(row->accent), 0);
        lv_obj_set_style_text_font(
            title, settings_page_font_or_default(page->title_font), 0);
        lv_obj_set_width(title, LV_PCT(100));
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    }
    else {
        int32_t text_width = row->kind == SETTINGS_PAGE_ROW_SLIDER
                                 ? 36
                                 : (row->kind == SETTINGS_PAGE_ROW_CHOICE
                                        ? 48
                                        : 60);

        lv_obj_set_width(title, LV_PCT(text_width));
        lv_obj_align(title, LV_ALIGN_LEFT_MID, 54, row->subtitle ? -8 : 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 14, 0);
    }

    if(!grid && row->subtitle != NULL && row->subtitle[0] != '\0') {
        lv_obj_t *subtitle = lv_label_create(row_object);

        lv_label_set_text(subtitle, row->subtitle);
        settings_page_style_body_label(page, subtitle, true);
        lv_obj_set_width(
            subtitle,
            LV_PCT(row->kind == SETTINGS_PAGE_ROW_SLIDER ? 36 : 60));
        lv_obj_align(subtitle, LV_ALIGN_LEFT_MID, 54, 16);
    }
}

static void settings_page_add_list_accessory(
    settings_page_t *page,
    uint8_t row_index,
    lv_obj_t *row_object)
{
    settings_page_row_t *row = &page->rows[row_index];

    if(row->kind == SETTINGS_PAGE_ROW_SWITCH) {
        lv_obj_t *switch_object = lv_switch_create(row_object);

        page->switch_objects[row_index] = switch_object;
        lv_obj_set_size(switch_object, 62, 32);
        lv_obj_align(switch_object, LV_ALIGN_RIGHT_MID, -16, 0);
        lv_obj_set_style_bg_color(
            switch_object, page->theme.panel, LV_PART_MAIN);
        lv_obj_set_style_bg_color(
            switch_object, page->theme.accent, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(
            switch_object, page->theme.accent, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(
            switch_object, page->theme.text, LV_PART_KNOB);
        lv_obj_add_event_cb(
            switch_object,
            settings_page_on_switch_changed,
            LV_EVENT_VALUE_CHANGED,
            &page->bindings[row_index]);
        settings_page_add_key_handler(page, switch_object);
    }
    else if(row->kind == SETTINGS_PAGE_ROW_SLIDER) {
        lv_obj_t *slider = lv_slider_create(row_object);
        lv_obj_t *value = lv_label_create(row_object);

        page->slider_objects[row_index] = slider;
        page->value_labels[row_index] = value;
        lv_obj_set_width(slider, 300);
        lv_obj_align(slider, LV_ALIGN_RIGHT_MID, -68, 0);
        lv_slider_set_range(slider, row->min_value, row->max_value);
        lv_slider_set_value(slider, row->value, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(slider, page->theme.panel, LV_PART_MAIN);
        lv_obj_set_style_bg_color(
            slider, page->theme.accent, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(
            slider, page->theme.text, LV_PART_KNOB);
        lv_obj_add_event_cb(
            slider,
            settings_page_on_slider_changed,
            LV_EVENT_VALUE_CHANGED,
            &page->bindings[row_index]);
        lv_obj_add_event_cb(
            slider,
            settings_page_on_slider_released,
            LV_EVENT_RELEASED,
            &page->bindings[row_index]);
        settings_page_add_key_handler(page, slider);
        lv_obj_set_width(value, 48);
        lv_obj_align(value, LV_ALIGN_RIGHT_MID, -14, 0);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        settings_page_style_body_label(page, value, true);
    }
    else if(row->kind == SETTINGS_PAGE_ROW_CHOICE) {
        lv_obj_t *choice = lv_dropdown_create(row_object);

        page->choice_objects[row_index] = choice;
        lv_obj_set_size(choice, 190, 44);
        lv_obj_align(choice, LV_ALIGN_RIGHT_MID, -14, 0);
        lv_dropdown_clear_options(choice);
        for(uint8_t option_index = 0u;
            option_index < row->option_count;
            ++option_index) {
            lv_dropdown_add_option(
                choice,
                row->options[option_index] != NULL
                    ? row->options[option_index]
                    : "-",
                LV_DROPDOWN_POS_LAST);
        }
        lv_dropdown_set_selected(choice, row->selected_option);
        lv_obj_set_style_text_font(
            choice, settings_page_font_or_default(page->body_font), 0);
        lv_obj_set_style_bg_color(choice, page->theme.panel, 0);
        lv_obj_set_style_text_color(choice, lv_color_hex(0xFFFFFF), 0);
        lv_obj_add_event_cb(
            choice,
            settings_page_on_choice_changed,
            LV_EVENT_VALUE_CHANGED,
            &page->bindings[row_index]);
        settings_page_add_key_handler(page, choice);
    }
    else if(row->kind == SETTINGS_PAGE_ROW_NAVIGATION ||
            row->kind == SETTINGS_PAGE_ROW_ACTION) {
        lv_obj_t *arrow;
        lv_obj_t *value = lv_label_create(row_object);

        if(settings_page_is_product(page) && settings_page_asset_arrow(page) != NULL) {
            arrow = settings_page_create_image(
                row_object, settings_page_asset_arrow(page), 14u, 24u);
        }
        else {
            arrow = lv_label_create(row_object);
            lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        }
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -14, 0);
        settings_page_style_body_label(page, arrow, true);
        page->value_labels[row_index] = value;
        lv_obj_set_width(value, LV_PCT(30));
        lv_obj_align(value, LV_ALIGN_RIGHT_MID, -48, 0);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        settings_page_style_body_label(page, value, true);
    }
    else if(row->kind == SETTINGS_PAGE_ROW_VALUE) {
        lv_obj_t *value = lv_label_create(row_object);

        page->value_labels[row_index] = value;
        lv_obj_set_width(value, LV_PCT(34));
        lv_obj_align(value, LV_ALIGN_RIGHT_MID, -16, 0);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        settings_page_style_body_label(page, value, true);
    }
}

static void settings_page_add_grid_accessory(
    settings_page_t *page,
    uint8_t row_index,
    lv_obj_t *row_object)
{
    const settings_page_row_t *row = &page->rows[row_index];

    lv_obj_t *subtitle = lv_label_create(row_object);

    page->value_labels[row_index] = subtitle;
    lv_label_set_text(
        subtitle,
        row->value_text != NULL && row->value_text[0] != '\0'
            ? row->value_text
            : (row->subtitle != NULL ? row->subtitle : ""));
    lv_obj_set_width(subtitle, LV_PCT(100));
    lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(
        subtitle, settings_page_tile_foreground(row->accent), 0);
    lv_obj_set_style_text_opa(subtitle, LV_OPA_70, 0);
    lv_obj_set_style_text_font(
        subtitle, settings_page_font_or_default(page->body_font), 0);
}

static const lv_point_t settings_page_product_positions[] = {
    {150, 80},
    {340, 80},
    {564, 80},
    {150, 280},
    {357, 280},
    {564, 280},
};

static lv_obj_t *settings_page_create_product_overview_row(
    settings_page_t *page, uint8_t row_index)
{
    const settings_page_row_t *row = &page->rows[row_index];
    uint16_t configured_item_width = page->assets.overview_item_width;
    uint16_t item_height = settings_page_asset_or_default(
        page->assets.overview_item_height, 160u);
    uint16_t icon_width = settings_page_asset_or_default(
        page->assets.overview_icon_width, 102u);
    uint16_t icon_height = settings_page_asset_or_default(
        page->assets.overview_icon_height, 100u);
    uint16_t label_gap = settings_page_asset_or_default(
        page->assets.overview_label_gap, 20u);
    lv_obj_t *item = lv_button_create(page->content);
    lv_obj_t *icon;
    lv_obj_t *title;

    if(item == NULL) return NULL;
    settings_page_make_plain(item);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(
        item,
        configured_item_width != 0u ? configured_item_width : LV_SIZE_CONTENT,
        item_height);
    lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(item, 0, 0);
    lv_obj_set_style_pad_all(item, 0, 0);
    lv_obj_set_style_pad_row(item, label_gap, 0);
    lv_obj_set_style_opa(item, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_opa(item, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_transform_scale(item, 270, LV_STATE_PRESSED);
    lv_obj_set_style_transform_pivot_x(item, 50, LV_STATE_PRESSED);
    lv_obj_set_style_transform_pivot_y(item, 50, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        item,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    if(row->image_src != NULL) {
        icon = settings_page_create_image(
            item, row->image_src, icon_width, icon_height);
    }
    else {
        icon = lv_label_create(item);
        lv_label_set_text(
            icon, row->symbol != NULL ? row->symbol : LV_SYMBOL_SETTINGS);
        lv_obj_set_style_text_font(
            icon,
            settings_page_font_or_default(
                page->overview_font != NULL
                    ? page->overview_font
                    : page->title_font),
            0);
        lv_obj_set_style_text_color(icon, page->theme.text, 0);
        lv_obj_set_size(icon, icon_width, icon_height);
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(icon, 30, 0);
    }
    if(icon == NULL) {
        lv_obj_delete(item);
        return NULL;
    }
    title = lv_label_create(item);
    if(title == NULL) {
        lv_obj_delete(item);
        return NULL;
    }
    lv_label_set_text(title, row->title != NULL ? row->title : "");
    lv_obj_set_width(
        title,
        configured_item_width != 0u ? configured_item_width : LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title, page->theme.text, 0);
    lv_obj_set_style_text_font(
        title,
        settings_page_font_or_default(
            page->overview_font != NULL ? page->overview_font : page->title_font),
        0);
    settings_page_add_key_handler(page, item);
    return item;
}

static void settings_page_position_product_overview(
    settings_page_t *page, lv_obj_t *item, uint8_t row_index)
{
    const lv_point_t *positions = settings_page_product_positions;
    uint8_t position_count =
        (uint8_t)(sizeof(settings_page_product_positions) /
                  sizeof(settings_page_product_positions[0]));

    if(page->assets.overview_positions != NULL &&
       page->assets.overview_position_count >= page->row_count) {
        positions = page->assets.overview_positions;
        position_count = page->assets.overview_position_count;
    }
    if(row_index < position_count) {
        lv_obj_set_pos(item, positions[row_index].x, positions[row_index].y);
    }
    else {
        uint8_t column = row_index % SETTINGS_PAGE_GRID_COLUMN_COUNT;
        uint8_t row = row_index / SETTINGS_PAGE_GRID_COLUMN_COUNT;
        lv_obj_set_pos(item, 150 + column * 207, 80 + row * 200);
    }
}

static int8_t settings_page_find_kind(
    const settings_page_t *page, settings_page_row_kind_t kind,
    uint8_t occurrence)
{
    uint8_t found = 0u;

    for(uint8_t row_index = 0u; row_index < page->row_count; ++row_index) {
        if(page->rows[row_index].kind != kind) continue;
        if(found == occurrence) return (int8_t)row_index;
        ++found;
    }
    return -1;
}

static lv_obj_t *settings_page_create_product_label(
    settings_page_t *page, const char *text, int32_t x, int32_t y,
    int32_t width, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(page->content);

    if(label == NULL) return NULL;
    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_set_size(label, width, LV_SIZE_CONTENT);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, page->theme.text, 0);
    lv_obj_set_style_text_font(
        label, settings_page_font_or_default(font), 0);
    return label;
}

static lv_obj_t *settings_page_create_product_center_label(
    settings_page_t *page, const char *text, int32_t x, int32_t y,
    int32_t width, const lv_font_t *font)
{
    lv_obj_t *label = settings_page_create_product_label(
        page, text, x, y, width, font);

    if(label != NULL) {
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
    return label;
}

static void settings_page_style_product_slider(
    settings_page_t *page, lv_obj_t *slider)
{
    lv_obj_set_style_radius(slider, 12, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 12, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFEBCB), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        slider, lv_color_hex(0x2EE4F9), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 1, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, lv_color_hex(0xE6CFAE), LV_PART_KNOB);
    if(page->assets.slider_knob_src != NULL) {
        lv_obj_set_style_bg_image_src(
            slider, page->assets.slider_knob_src, LV_PART_KNOB);
        lv_obj_set_style_bg_image_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    }
    lv_obj_set_style_width(slider, 52, LV_PART_KNOB);
    lv_obj_set_style_height(slider, 52, LV_PART_KNOB);
}

static lv_obj_t *settings_page_create_product_step_button(
    settings_page_t *page, uint8_t row_index, int32_t x, int32_t y,
    const void *source)
{
    lv_obj_t *button = lv_button_create(page->content);
    lv_obj_t *image;

    if(button == NULL) return NULL;
    settings_page_make_plain(button);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(button, 54, 54);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, 9, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xFFC243), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(button, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_opa(button, LV_OPA_40, LV_STATE_DISABLED);
    image = settings_page_create_image(button, source, 54u, 54u);
    if(image != NULL) lv_obj_center(image);
    lv_obj_add_event_cb(
        button,
        settings_page_on_step_clicked,
        LV_EVENT_CLICKED,
        &page->bindings[row_index]);
    settings_page_add_key_handler(page, button);
    return button;
}

static lv_obj_t *settings_page_create_product_choice(
    settings_page_t *page, uint8_t row_index, uint8_t option_index,
    int32_t x, int32_t y)
{
    const settings_page_row_t *row = &page->rows[row_index];
    lv_obj_t *button = lv_button_create(page->content);
    lv_obj_t *label;

    if(button == NULL) return NULL;
    settings_page_make_plain(button);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(button, 116, 50);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, 25, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xFFECC8), 0);
    lv_obj_set_style_bg_color(
        button, lv_color_hex(0xFEB51C), LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(
        button, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_opa(button, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_opa(button, LV_OPA_40, LV_STATE_DISABLED);
    label = lv_label_create(button);
    if(label == NULL) {
        lv_obj_delete(button);
        return NULL;
    }
    lv_label_set_text(
        label,
        row->options[option_index] != NULL ? row->options[option_index] : "-");
    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, page->theme.text, 0);
    lv_obj_set_style_text_font(
        label, settings_page_font_or_default(page->body_font), 0);
    page->choice_option_objects[row_index][option_index] = button;
    page->option_bindings[row_index][option_index].page = page;
    page->option_bindings[row_index][option_index].row_index = row_index;
    page->option_bindings[row_index][option_index].option_index = option_index;
    lv_obj_add_event_cb(
        button,
        settings_page_on_choice_option_clicked,
        LV_EVENT_CLICKED,
        &page->option_bindings[row_index][option_index]);
    settings_page_add_key_handler(page, button);
    if(option_index == row->selected_option) {
        lv_obj_add_state(button, LV_STATE_CHECKED);
    }
    return button;
}

static void settings_page_create_product_choice_line(
    settings_page_t *page, int32_t x, int32_t y, int32_t width)
{
    lv_obj_t *line = lv_obj_create(page->content);

    if(line == NULL) return;
    settings_page_make_plain(line);
    lv_obj_set_size(line, width, 6);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_style_radius(line, 3, 0);
    lv_obj_set_style_bg_color(line, lv_color_hex(0xFFECC8), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
}

static lv_obj_t *settings_page_create_product_slider(
    settings_page_t *page, uint8_t row_index, int32_t x, int32_t y,
    int32_t width)
{
    const settings_page_row_t *row = &page->rows[row_index];
    lv_obj_t *slider = lv_slider_create(page->content);

    if(slider == NULL) return NULL;
    page->slider_objects[row_index] = slider;
    lv_obj_set_size(slider, width, 24);
    lv_obj_set_pos(slider, x, y);
    lv_slider_set_range(slider, row->min_value, row->max_value);
    lv_slider_set_value(slider, row->value, LV_ANIM_OFF);
    settings_page_style_product_slider(page, slider);
    lv_obj_add_event_cb(
        slider,
        settings_page_on_slider_changed,
        LV_EVENT_VALUE_CHANGED,
        &page->bindings[row_index]);
    lv_obj_add_event_cb(
        slider,
        settings_page_on_slider_released,
        LV_EVENT_RELEASED,
        &page->bindings[row_index]);
    settings_page_add_key_handler(page, slider);
    return slider;
}

static bool settings_page_build_product_brightness(
    settings_page_t *page)
{
    int8_t slider_index = settings_page_find_kind(
        page, SETTINGS_PAGE_ROW_SLIDER, 0u);
    int8_t screen_off_index = settings_page_find_kind(
        page, SETTINGS_PAGE_ROW_CHOICE, 0u);
    int8_t shutdown_index = settings_page_find_kind(
        page, SETTINGS_PAGE_ROW_CHOICE, 1u);

    if(slider_index < 0 || screen_off_index < 0 || shutdown_index < 0) {
        return false;
    }
    (void)settings_page_create_product_center_label(
        page, "亮度调节", 80, 59, 200, page->title_font);
    if(settings_page_create_product_slider(
           page, (uint8_t)slider_index, 197, 96, 420) == NULL) {
        return false;
    }
    page->decrement_button = settings_page_create_product_step_button(
        page, (uint8_t)slider_index, 122, 81, page->assets.decrement_src);
    page->increment_button = settings_page_create_product_step_button(
        page, (uint8_t)slider_index, 636, 81, page->assets.increment_src);
    if(page->decrement_button == NULL || page->increment_button == NULL) {
        return false;
    }

    (void)settings_page_create_product_center_label(
        page, "熄屏时间", 80, 189, 200, page->title_font);
    settings_page_create_product_choice_line(page, 145, 233, 470);
    for(uint8_t option_index = 0u;
        option_index < page->rows[(uint8_t)screen_off_index].option_count;
        ++option_index) {
        if(settings_page_create_product_choice(
               page, (uint8_t)screen_off_index, option_index,
               133 + option_index * 146, 211) == NULL) {
            return false;
        }
    }

    (void)settings_page_create_product_center_label(
        page, "关机时间", 80, 316, 200, page->title_font);
    settings_page_create_product_choice_line(page, 145, 359, 470);
    for(uint8_t option_index = 0u;
        option_index < page->rows[(uint8_t)shutdown_index].option_count;
        ++option_index) {
        if(settings_page_create_product_choice(
               page, (uint8_t)shutdown_index, option_index,
               133 + option_index * 146, 337) == NULL) {
            return false;
        }
    }
    return true;
}

static bool settings_page_build_product_volume(settings_page_t *page)
{
    int8_t slider_index = settings_page_find_kind(
        page, SETTINGS_PAGE_ROW_SLIDER, 0u);

    if(slider_index < 0) return false;
    if(settings_page_create_product_slider(
           page, (uint8_t)slider_index, 140, 200, 530) == NULL) {
        return false;
    }
    page->decrement_button = settings_page_create_product_step_button(
        page, (uint8_t)slider_index, 130, 250, page->assets.decrement_src);
    page->increment_button = settings_page_create_product_step_button(
        page, (uint8_t)slider_index, 626, 250, page->assets.increment_src);
    return page->decrement_button != NULL && page->increment_button != NULL;
}

static bool settings_page_build_product_mobile_network(settings_page_t *page)
{
    int8_t switch_index = settings_page_find_kind(
        page, SETTINGS_PAGE_ROW_SWITCH, 0u);
    lv_obj_t *container;
    lv_obj_t *controller;
    lv_obj_t *label;
    lv_obj_t *switch_object;

    if(switch_index < 0) return false;
    container = lv_obj_create(page->content);
    if(container == NULL) return false;
    settings_page_add_key_handler(page, container);
    lv_obj_set_size(container, 615, LV_PCT(80));
    lv_obj_align(container, LV_ALIGN_BOTTOM_MID, 10, -20);
    lv_obj_set_style_radius(container, 25, 0);
    lv_obj_set_style_bg_color(container, lv_color_hex(0xFFF9EB), 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);

    controller = lv_obj_create(container);
    if(controller == NULL) return false;
    lv_obj_set_size(controller, LV_PCT(95), 70);
    lv_obj_align(controller, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_radius(controller, 8, 0);
    lv_obj_set_style_bg_color(controller, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(controller, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(controller, lv_color_hex(0xFFECC8), 0);
    lv_obj_set_style_border_width(controller, 1, 0);
    lv_obj_set_style_pad_hor(controller, 20, 0);
    lv_obj_set_style_pad_top(controller, 10, 0);
    lv_obj_set_style_pad_bottom(controller, 0, 0);

    label = lv_label_create(controller);
    if(label == NULL) return false;
    lv_label_set_text(
        label,
        page->rows[(uint8_t)switch_index].title != NULL
            ? page->rows[(uint8_t)switch_index].title
            : "移动网络");
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, -5);
    lv_obj_set_style_text_color(label, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_font(
        label, settings_page_font_or_default(page->title_font), 0);

    switch_object = lv_switch_create(controller);
    if(switch_object == NULL) return false;
    page->row_objects[(uint8_t)switch_index] = controller;
    page->switch_objects[(uint8_t)switch_index] = switch_object;
    lv_obj_set_size(switch_object, 70, 35);
    lv_obj_align(switch_object, LV_ALIGN_RIGHT_MID, 0, -5);
    lv_obj_set_style_opa(switch_object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(switch_object, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        switch_object, lv_color_hex(0xE6E6E6), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(switch_object, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(
        switch_object, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(
        switch_object, lv_color_hex(0xE6E6E6), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(
        switch_object, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(
        switch_object, lv_color_hex(0x01D5ED),
        LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(
        switch_object, LV_OPA_COVER,
        LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_radius(switch_object, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(
        switch_object, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(
        switch_object, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(switch_object, -4, LV_PART_KNOB);
    lv_obj_set_style_anim_duration(switch_object, 120, LV_PART_MAIN);
    lv_obj_add_event_cb(
        switch_object,
        settings_page_on_switch_changed,
        LV_EVENT_VALUE_CHANGED,
        &page->bindings[(uint8_t)switch_index]);
    settings_page_add_key_handler(page, switch_object);
    return true;
}

static lv_obj_t *settings_page_create_product_wifi_item(
    settings_page_t *page, lv_obj_t *parent, uint8_t row_index,
    int32_t y, bool current)
{
    const settings_page_row_t *row = &page->rows[row_index];
    lv_obj_t *item = lv_button_create(parent);
    lv_obj_t *title;
    lv_obj_t *icon;
    lv_obj_t *status;
    const void *source;

    if(item == NULL) return NULL;
    settings_page_make_plain(item);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(item, current ? 578 : 580, current ? 64 : 65);
    lv_obj_set_pos(item, current ? 1 : 20, y);
    lv_obj_set_style_bg_color(item, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(item, 0, 0);
    lv_obj_set_style_opa(item, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_opa(item, LV_OPA_40, LV_STATE_DISABLED);

    title = lv_label_create(item);
    if(title == NULL) return NULL;
    lv_label_set_text(title, row->title != NULL ? row->title : "");
    lv_obj_set_width(title, 380);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_text_color(title, page->theme.text, 0);
    lv_obj_set_style_text_font(
        title,
        settings_page_font_or_default(
            page->emphasis_font != NULL ? page->emphasis_font
                                         : page->title_font),
        0);

    page->wifi_current[row_index] = current;

    source = settings_page_asset_wifi_connected(page);
    if(source != NULL) {
        icon = settings_page_create_image(item, source, 28u, 22u);
    }
    else {
        icon = lv_label_create(item);
        if(icon != NULL) {
            lv_label_set_text(icon, LV_SYMBOL_OK);
            lv_obj_set_style_text_color(icon, lv_color_hex(0x2EE4F9), 0);
            lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, 0);
        }
    }
    if(icon == NULL) return NULL;
    lv_obj_align(icon, LV_ALIGN_RIGHT_MID, current ? -60 : -52, 0);
    page->wifi_connected_objects[row_index] = icon;

    source = settings_page_asset_wifi_encrypted(page);
    if(source != NULL) {
        icon = settings_page_create_image(item, source, 15u, 20u);
    }
    else {
        icon = lv_label_create(item);
        if(icon != NULL) {
            lv_label_set_text(icon, "*");
            lv_obj_set_style_text_color(icon, lv_color_hex(0xEDA55F), 0);
            lv_obj_set_style_text_font(
                icon, settings_page_font_or_default(page->body_font), 0);
        }
    }
    if(icon == NULL) return NULL;
    lv_obj_align(icon, LV_ALIGN_RIGHT_MID, current ? -94 : -45, 0);
    page->wifi_encrypted_objects[row_index] = icon;

    source = settings_page_asset_wifi_signal(page, row);
    if(source != NULL) {
        icon = settings_page_create_image(item, source, 28u, 20u);
    }
    else {
        icon = lv_label_create(item);
        if(icon != NULL) {
            lv_label_set_text(icon, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(icon, lv_color_hex(0x3388F4), 0);
            lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, 0);
        }
    }
    if(icon == NULL) return NULL;
    lv_obj_align(icon, LV_ALIGN_RIGHT_MID, current ? -12 : -10, 0);
    page->wifi_signal_objects[row_index] = icon;

    status = lv_label_create(item);
    if(status == NULL) return NULL;
    lv_obj_set_width(status, 110);
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, -112, 0);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(status, page->theme.muted_text, 0);
    lv_obj_set_style_text_font(
        status, settings_page_font_or_default(page->body_font), 0);
    page->wifi_status_labels[row_index] = status;
    settings_page_set_hidden(status, true);
    lv_obj_add_event_cb(
        item,
        settings_page_on_row_clicked,
        LV_EVENT_CLICKED,
        &page->bindings[row_index]);
    lv_obj_add_event_cb(
        item,
        settings_page_on_row_long_pressed,
        LV_EVENT_LONG_PRESSED,
        &page->bindings[row_index]);
    lv_obj_add_event_cb(
        item,
        settings_page_on_row_press_lost,
        LV_EVENT_PRESS_LOST,
        &page->bindings[row_index]);
    settings_page_add_key_handler(page, item);
    page->row_objects[row_index] = item;
    return item;
}

static bool settings_page_build_product_wifi(settings_page_t *page)
{
    int8_t switch_index = settings_page_find_kind(
        page, SETTINGS_PAGE_ROW_SWITCH, 0u);
    lv_obj_t *surface;
    lv_obj_t *current_panel;
    lv_obj_t *controller;
    lv_obj_t *label;
    lv_obj_t *switch_object;
    lv_obj_t *spinner;
    const char *previous_section = NULL;
    int32_t cursor;
    int8_t current_index = -1;
    bool has_network = false;

    if(switch_index < 0) return false;
    for(uint8_t row_index = 0u; row_index < page->row_count; ++row_index) {
        if(row_index != (uint8_t)switch_index &&
           page->rows[row_index].section == NULL) {
            current_index = (int8_t)row_index;
            break;
        }
    }
    surface = lv_obj_create(page->content);
    if(surface == NULL) return false;
    settings_page_make_plain(surface);
    settings_page_add_key_handler(page, surface);
    lv_obj_set_size(surface, 620, 353);
    lv_obj_set_pos(surface, 97, 67);
    lv_obj_add_flag(surface, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(surface, LV_DIR_VER);
    lv_obj_set_style_bg_color(surface, lv_color_hex(0xFFF9EB), 0);
    lv_obj_set_style_bg_opa(surface, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(surface, 0, 0);
    lv_obj_remove_flag(surface, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(surface, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scrollbar_mode(surface, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(surface, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_top(surface, 7, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_bottom(surface, 7, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(surface, 6, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(
        surface, lv_color_hex(0xFFEBA2), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(surface, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(surface, 2, LV_PART_SCROLLBAR);

#if LV_USE_SPINNER
    spinner = lv_spinner_create(page->content);
    if(spinner == NULL) return false;
    page->wifi_loading_object = spinner;
    lv_obj_set_size(spinner, 48, 48);
    lv_obj_set_pos(spinner, 376, 188);
    lv_spinner_set_anim_params(spinner, 900, 90);
    lv_obj_set_style_arc_color(
        spinner, lv_color_hex(0x01D5ED), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(
        spinner, lv_color_hex(0xDCEFF7), LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(spinner, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(spinner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flag(
        spinner, LV_OBJ_FLAG_HIDDEN, !page->wifi_loading);
#else
    spinner = NULL;
    page->wifi_loading_object = NULL;
#endif

    current_panel = lv_obj_create(surface);
    if(current_panel == NULL) return false;
    settings_page_make_plain(current_panel);
    lv_obj_set_size(current_panel, 580, current_index >= 0 ? 136 : 68);
    lv_obj_set_pos(current_panel, 20, 21);
    lv_obj_set_style_radius(current_panel, 8, 0);
    lv_obj_set_style_bg_color(current_panel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(current_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(current_panel, lv_color_hex(0xFFECC8), 0);
    lv_obj_set_style_border_width(current_panel, 1, 0);
    lv_obj_set_style_clip_corner(current_panel, true, 0);

    controller = lv_obj_create(current_panel);
    if(controller == NULL) return false;
    settings_page_make_plain(controller);
    lv_obj_set_size(controller, 578, 64);
    lv_obj_set_pos(controller, 1, 2);
    lv_obj_set_style_radius(controller, 0, 0);
    lv_obj_set_style_bg_color(controller, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(controller, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(controller, 0, 0);
    lv_obj_set_style_pad_all(controller, 0, 0);

    label = lv_label_create(controller);
    if(label == NULL) return false;
    lv_label_set_text(
        label,
        page->rows[(uint8_t)switch_index].title != NULL
            ? page->rows[(uint8_t)switch_index].title
            : "无线局域网");
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_font(
        label, settings_page_font_or_default(page->title_font), 0);

    switch_object = lv_switch_create(controller);
    if(switch_object == NULL) return false;
    page->row_objects[(uint8_t)switch_index] = controller;
    page->switch_objects[(uint8_t)switch_index] = switch_object;
    lv_obj_set_size(switch_object, 70, 35);
    lv_obj_align(switch_object, LV_ALIGN_RIGHT_MID, -22, 0);
    lv_obj_set_style_opa(switch_object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(switch_object, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        switch_object, lv_color_hex(0xE6E6E6), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(switch_object, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(
        switch_object, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(
        switch_object, lv_color_hex(0xE6E6E6), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(
        switch_object, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(
        switch_object, lv_color_hex(0x01D5ED),
        LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(
        switch_object, LV_OPA_COVER,
        LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_radius(switch_object, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(
        switch_object, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(
        switch_object, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(switch_object, -4, LV_PART_KNOB);
    lv_obj_set_style_anim_duration(switch_object, 120, LV_PART_MAIN);
    lv_obj_add_event_cb(
        switch_object,
        settings_page_on_switch_changed,
        LV_EVENT_VALUE_CHANGED,
        &page->bindings[(uint8_t)switch_index]);
    settings_page_add_key_handler(page, switch_object);

    if(current_index >= 0) {
        lv_obj_t *separator = lv_obj_create(current_panel);

        if(separator == NULL) return false;
        settings_page_make_plain(separator);
        lv_obj_set_size(separator, 578, 2);
        lv_obj_set_pos(separator, 1, 66);
        lv_obj_set_style_bg_color(separator, page->theme.background, 0);
        lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
    }

    cursor = current_index >= 0 ? 154 : 89;
    for(uint8_t row_index = 0u; row_index < page->row_count; ++row_index) {
        const settings_page_row_t *row = &page->rows[row_index];

        if(row_index == (uint8_t)switch_index) continue;
        if(row_index == (uint8_t)current_index) {
            if(settings_page_create_product_wifi_item(
                   page, current_panel, row_index, 68, true) == NULL) {
                return false;
            }
            has_network = true;
            continue;
        }
        if(row->section != NULL &&
           (previous_section == NULL ||
            strcmp(previous_section, row->section) != 0)) {
            lv_obj_t *section = lv_label_create(surface);

            if(has_network) cursor += previous_section == NULL ? 14 : 11;
            if(section == NULL) return false;
            lv_label_set_text(section, row->section);
            lv_obj_set_size(
                section,
                row->section_subtitle != NULL &&
                        row->section_subtitle[0] != '\0'
                    ? 112
                    : 580,
                30);
            lv_obj_set_pos(section, 22, cursor + 1);
            lv_obj_set_style_text_color(section, lv_color_hex(0x333333), 0);
            lv_obj_set_style_text_font(
                section, settings_page_font_or_default(page->body_font), 0);
            if(row->section_subtitle != NULL &&
               row->section_subtitle[0] != '\0') {
                lv_obj_t *subtitle = lv_label_create(surface);

                if(subtitle == NULL) return false;
                lv_label_set_text(subtitle, row->section_subtitle);
                lv_obj_set_size(subtitle, 460, 24);
                lv_obj_set_pos(subtitle, 139, cursor + 7);
                lv_obj_set_style_text_color(subtitle, lv_color_hex(0x333333), 0);
                lv_obj_set_style_text_font(
                    subtitle,
                    settings_page_font_or_default(
                        page->section_font != NULL
                            ? page->section_font
                            : page->body_font),
                    0);
            }
            previous_section = row->section;
            cursor += 30;
        }
        if(settings_page_create_product_wifi_item(
               page, surface, row_index, cursor,
               !has_network && row->section == NULL) == NULL) {
            return false;
        }
        cursor += 65;
        has_network = true;
    }

    if(spinner != NULL) lv_obj_move_foreground(spinner);
    return true;
}

static lv_obj_t *settings_page_create_product_menu_row(
    settings_page_t *page, uint8_t row_index, int32_t x, int32_t y,
    int32_t width)
{
    const settings_page_row_t *row = &page->rows[row_index];
    lv_obj_t *item = lv_button_create(page->content);
    lv_obj_t *icon;
    lv_obj_t *title;
    lv_obj_t *value;
    lv_obj_t *arrow;

    if(item == NULL) return NULL;
    settings_page_make_plain(item);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(item, width, 80);
    lv_obj_set_pos(item, x, y);
    lv_obj_set_style_radius(item, 15, 0);
    lv_obj_set_style_bg_color(item, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(item, 1, 0);
    lv_obj_set_style_border_color(item, lv_color_hex(0xFFECC8), 0);
    lv_obj_set_style_opa(item, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_opa(item, LV_OPA_40, LV_STATE_DISABLED);

    if(row->image_src != NULL) {
        icon = settings_page_create_image(item, row->image_src, 36u, 36u);
    }
    else {
        icon = lv_label_create(item);
        lv_label_set_text(
            icon, row->symbol != NULL ? row->symbol : LV_SYMBOL_SETTINGS);
        lv_obj_set_style_text_font(icon, page->body_font, 0);
        lv_obj_set_style_text_color(icon, page->theme.accent, 0);
    }
    if(icon == NULL) return NULL;
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 25, 0);

    title = lv_label_create(item);
    if(title == NULL) return NULL;
    lv_label_set_text(title, row->title != NULL ? row->title : "");
    lv_obj_set_width(title, 280);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 90, 0);
    lv_obj_set_style_text_color(title, page->theme.text, 0);
    lv_obj_set_style_text_font(
        title, settings_page_font_or_default(page->title_font), 0);

    value = lv_label_create(item);
    if(value == NULL) return NULL;
    page->value_labels[row_index] = value;
    lv_obj_set_size(value, 100, 30);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, -98, 0);
    lv_obj_set_style_radius(value, 15, 0);
    lv_obj_set_style_bg_color(value, lv_color_hex(0x61DFE4), 0);
    lv_obj_set_style_bg_opa(
        value,
        row->value_text != NULL && row->value_text[0] != '\0'
            ? LV_OPA_50
            : LV_OPA_TRANSP,
        0);
    lv_obj_set_style_text_color(value, page->theme.text, 0);
    lv_obj_set_style_text_font(
        value, settings_page_font_or_default(page->body_font), 0);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);

    arrow = settings_page_create_image(
        item, settings_page_asset_arrow(page), 14u, 24u);
    if(arrow == NULL) {
        arrow = lv_label_create(item);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(arrow, page->theme.accent, 0);
    }
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -30, 0);
    lv_obj_add_event_cb(
        item,
        settings_page_on_row_clicked,
        LV_EVENT_CLICKED,
        &page->bindings[row_index]);
    lv_obj_add_event_cb(
        item,
        settings_page_on_row_long_pressed,
        LV_EVENT_LONG_PRESSED,
        &page->bindings[row_index]);
    lv_obj_add_event_cb(
        item,
        settings_page_on_row_press_lost,
        LV_EVENT_PRESS_LOST,
        &page->bindings[row_index]);
    settings_page_add_key_handler(page, item);
    return item;
}

static bool settings_page_build_product_about(settings_page_t *page)
{
    for(uint8_t row_index = 0u; row_index < page->row_count; ++row_index) {
        page->row_objects[row_index] = settings_page_create_product_menu_row(
            page, row_index, 119, 72 + row_index * 90, 576);
        if(page->row_objects[row_index] == NULL) return false;
    }
    return true;
}

static bool settings_page_build_product_storage(settings_page_t *page)
{
    lv_obj_t *bar;
    lv_obj_t *free_label;
    lv_obj_t *total_label;
    int32_t percent;

    if(page->row_count == 0u) return false;
    percent = page->rows[0].value;
    if(percent < 0) percent = 0;
    if(percent > 100) percent = 100;
    bar = lv_bar_create(page->content);
    if(bar == NULL) return false;
    page->row_objects[0] = bar;
    lv_obj_set_size(bar, 530, 24);
    lv_obj_set_pos(bar, 135, 228);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, percent, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 12, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 12, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFFEBCB), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        bar, lv_color_hex(0x2EE4F9), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);

    (void)settings_page_create_product_label(
        page,
        page->rows[0].title != NULL ? page->rows[0].title : "外部存储",
        130,
        169,
        360,
        page->emphasis_font != NULL ? page->emphasis_font : page->title_font);
    free_label = settings_page_create_product_label(
        page,
        page->rows[0].value_text != NULL ? page->rows[0].value_text : "",
        130,
        275,
        260,
        page->body_font);
    if(free_label != NULL) page->value_labels[0] = free_label;
    if(page->row_count > 1u) {
        total_label = settings_page_create_product_label(
            page,
            page->rows[1].value_text != NULL ? page->rows[1].value_text : "",
            480,
            275,
            190,
            page->body_font);
        if(total_label != NULL) {
            lv_obj_set_style_text_align(total_label, LV_TEXT_ALIGN_RIGHT, 0);
            page->value_labels[1] = total_label;
        }
    }
    return true;
}

static lv_obj_t *settings_page_create_product_value_row(
    settings_page_t *page, uint8_t row_index, lv_obj_t *container,
    int32_t y)
{
    const settings_page_row_t *row = &page->rows[row_index];
    lv_obj_t *item = lv_obj_create(container);
    lv_obj_t *title;
    lv_obj_t *value;
    lv_obj_t *arrow;
    bool interactive;

    if(item == NULL) return NULL;
    settings_page_make_plain(item);
    lv_obj_set_size(item, 600, 64);
    lv_obj_set_pos(item, 0, y);
    lv_obj_set_style_bg_color(item, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(item, 0, 0);
    interactive =
        row->kind == SETTINGS_PAGE_ROW_ACTION ||
        row->kind == SETTINGS_PAGE_ROW_NAVIGATION ||
        row->activation_clicks > 1u;
    if(interactive) {
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x888888), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, LV_OPA_20, LV_STATE_PRESSED);
        lv_obj_add_event_cb(
            item,
            settings_page_on_row_clicked,
            LV_EVENT_CLICKED,
            &page->bindings[row_index]);
        lv_obj_add_event_cb(
            item,
            settings_page_on_row_long_pressed,
            LV_EVENT_LONG_PRESSED,
            &page->bindings[row_index]);
        lv_obj_add_event_cb(
            item,
            settings_page_on_row_press_lost,
            LV_EVENT_PRESS_LOST,
            &page->bindings[row_index]);
        settings_page_add_key_handler(page, item);
    }
    title = lv_label_create(item);
    value = lv_label_create(item);
    if(title == NULL || value == NULL) return NULL;
    lv_label_set_text(title, row->title != NULL ? row->title : "");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 35, 0);
    lv_obj_set_style_text_color(title, page->theme.text, 0);
    lv_obj_set_style_text_font(
        title, settings_page_font_or_default(page->body_font), 0);
    lv_label_set_text(value, row->value_text != NULL ? row->value_text : "");
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, -37, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(0xEDA55F), 0);
    lv_obj_set_style_text_font(
        value, settings_page_font_or_default(page->title_font), 0);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    page->value_labels[row_index] = value;
    if(row->kind == SETTINGS_PAGE_ROW_ACTION ||
       row->kind == SETTINGS_PAGE_ROW_NAVIGATION) {
        lv_obj_add_flag(value, LV_OBJ_FLAG_HIDDEN);
        arrow = settings_page_create_image(
            item, settings_page_asset_arrow(page), 14u, 24u);
        if(arrow == NULL) {
            arrow = lv_label_create(item);
            if(arrow != NULL) {
                lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
                lv_obj_set_style_text_color(arrow, page->theme.accent, 0);
                lv_obj_set_style_text_font(
                    arrow, settings_page_font_or_default(page->body_font), 0);
            }
        }
        if(arrow != NULL) lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -20, 0);
    }
    return item;
}

static bool settings_page_build_product_about_device(settings_page_t *page)
{
    lv_obj_t *container = lv_obj_create(page->content);

    if(container == NULL) return false;
    settings_page_make_plain(container);
    settings_page_add_key_handler(page, container);
    lv_obj_set_size(container, 600, 400);
    lv_obj_set_pos(container, 107, 68);
    lv_obj_set_style_radius(container, 10, 0);
    lv_obj_set_style_bg_color(container, lv_color_hex(0xFFECC8), 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(container, lv_color_hex(0xFFECC8), 0);
    lv_obj_set_style_border_width(container, 1, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_pad_row(container, 1, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    for(uint8_t row_index = 0u; row_index < page->row_count; ++row_index) {
        page->row_objects[row_index] = settings_page_create_product_value_row(
            page, row_index, container, row_index * 64);
        if(page->row_objects[row_index] == NULL) return false;
    }
    return true;
}

static bool settings_page_build_product_upgrade(settings_page_t *page)
{
    int8_t action_index = settings_page_find_kind(
        page, SETTINGS_PAGE_ROW_ACTION, 0u);
    lv_obj_t *message;

    message = settings_page_create_product_center_label(
        page, "当前版本已是最新版本", 140, 260, 520, page->body_font);
    if(message == NULL) return false;
    if(action_index >= 0) {
        lv_obj_t *button = lv_button_create(page->content);
        lv_obj_t *label;

        if(button == NULL) return false;
        settings_page_make_plain(button);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(button, 170, 54);
        lv_obj_set_pos(button, 315, 310);
        lv_obj_set_style_radius(button, 27, 0);
        lv_obj_set_style_bg_color(
            button, lv_color_hex(SETTINGS_PAGE_PRODUCT_UPGRADE_BUTTON_COLOR), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_opa(button, LV_OPA_70, LV_STATE_PRESSED);
        lv_obj_set_style_opa(button, LV_OPA_40, LV_STATE_DISABLED);
        label = lv_label_create(button);
        if(label == NULL) return false;
        lv_label_set_text(
            label,
            page->rows[(uint8_t)action_index].title != NULL
                ? page->rows[(uint8_t)action_index].title
                : "立即更新");
        lv_obj_set_style_text_color(label, page->theme.text, 0);
        lv_obj_set_style_text_font(
            label, settings_page_font_or_default(page->title_font), 0);
        lv_obj_center(label);
        page->row_objects[(uint8_t)action_index] = button;
        lv_obj_add_event_cb(
            button,
            settings_page_on_row_clicked,
            LV_EVENT_CLICKED,
            &page->bindings[(uint8_t)action_index]);
        lv_obj_add_event_cb(
            button,
            settings_page_on_row_long_pressed,
            LV_EVENT_LONG_PRESSED,
            &page->bindings[(uint8_t)action_index]);
        lv_obj_add_event_cb(
            button,
            settings_page_on_row_press_lost,
            LV_EVENT_PRESS_LOST,
            &page->bindings[(uint8_t)action_index]);
        settings_page_add_key_handler(page, button);
    }
    return true;
}

static bool settings_page_build_product_variant(settings_page_t *page)
{
    switch(page->variant) {
        case SETTINGS_PAGE_VARIANT_UNIGUI_BRIGHTNESS:
            return settings_page_build_product_brightness(page);
        case SETTINGS_PAGE_VARIANT_UNIGUI_VOLUME:
            return settings_page_build_product_volume(page);
        case SETTINGS_PAGE_VARIANT_UNIGUI_ABOUT:
            return settings_page_build_product_about(page);
        case SETTINGS_PAGE_VARIANT_UNIGUI_STORAGE:
            return settings_page_build_product_storage(page);
        case SETTINGS_PAGE_VARIANT_UNIGUI_ABOUT_DEVICE:
            return settings_page_build_product_about_device(page);
        case SETTINGS_PAGE_VARIANT_UNIGUI_MOBILE_NETWORK:
            return settings_page_build_product_mobile_network(page);
        case SETTINGS_PAGE_VARIANT_UNIGUI_WIFI:
            return settings_page_build_product_wifi(page);
        case SETTINGS_PAGE_VARIANT_UNIGUI_UPGRADE:
            return settings_page_build_product_upgrade(page);
        case SETTINGS_PAGE_VARIANT_DEFAULT:
        default:
            return false;
    }
}

static bool settings_page_build_product_background(settings_page_t *page)
{
    if(!settings_page_is_product(page)) return true;
    if(settings_page_asset_background(page) != NULL) {
        page->background_image = settings_page_create_image(
            page->root, settings_page_asset_background(page), 0u, 0u);
        if(page->background_image == NULL) return false;
        lv_obj_center(page->background_image);
    }
    if(settings_page_asset_title_background(page) != NULL) {
        page->title_background_image = settings_page_create_image(
            page->root, settings_page_asset_title_background(page), 0u, 0u);
        if(page->title_background_image == NULL) return false;
        lv_obj_center(page->title_background_image);
    }
    return true;
}

static lv_obj_t *settings_page_create_grid_row(
    settings_page_t *page, uint8_t row_index)
{
    const settings_page_row_t *row = &page->rows[row_index];
    lv_obj_t *row_object = lv_button_create(page->content);

    settings_page_make_plain(row_object);
    if((row->kind == SETTINGS_PAGE_ROW_VALUE &&
        row->activation_clicks <= 1u) ||
       row->kind == SETTINGS_PAGE_ROW_SLIDER) {
        lv_obj_remove_flag(row_object, LV_OBJ_FLAG_CLICKABLE);
    }
    else {
        lv_obj_add_flag(row_object, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_set_width(row_object, LV_PCT(100));
    lv_obj_set_style_radius(row_object, 12, 0);
    lv_obj_set_style_bg_color(
        row_object, settings_page_accent_color(row->accent), 0);
    lv_obj_set_style_bg_opa(row_object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row_object, 1, 0);
    lv_obj_set_style_border_color(row_object, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(row_object, LV_OPA_30, 0);
    lv_obj_set_style_opa(row_object, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_opa(row_object, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_flex_flow(row_object, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        row_object,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(row_object, 10, 0);
    settings_page_add_row_text(page, row_object, row, true);
    settings_page_add_grid_accessory(page, row_index, row_object);
    return row_object;
}

static lv_obj_t *settings_page_create_list_row(
    settings_page_t *page, uint8_t row_index)
{
    const settings_page_row_t *row = &page->rows[row_index];
    lv_obj_t *row_object = lv_button_create(page->content);

    settings_page_make_plain(row_object);
    if((row->kind == SETTINGS_PAGE_ROW_VALUE &&
        row->activation_clicks <= 1u) ||
       row->kind == SETTINGS_PAGE_ROW_SLIDER) {
        lv_obj_remove_flag(row_object, LV_OBJ_FLAG_CLICKABLE);
    }
    else {
        lv_obj_add_flag(row_object, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_set_width(row_object, LV_PCT(100));
    lv_obj_set_height(row_object, SETTINGS_PAGE_LIST_ROW_HEIGHT);
    lv_obj_set_style_radius(row_object, 10, 0);
    lv_obj_set_style_bg_color(row_object, page->theme.row, 0);
    lv_obj_set_style_bg_opa(row_object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row_object, 1, 0);
    lv_obj_set_style_border_color(
        row_object,
        settings_page_is_product(page)
            ? lv_color_hex(0xC7E9FA)
            : page->theme.panel,
        0);
    lv_obj_set_style_border_opa(row_object, LV_OPA_30, 0);
    if(settings_page_is_product(page)) {
        lv_obj_set_height(row_object, 80);
        lv_obj_set_style_radius(row_object, 15, 0);
        lv_obj_set_style_bg_color(row_object, lv_color_hex(0xFFFFFF), 0);
    }
    lv_obj_set_style_opa(row_object, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_opa(row_object, LV_OPA_40, LV_STATE_DISABLED);
    settings_page_add_row_text(page, row_object, row, false);
    settings_page_add_list_accessory(page, row_index, row_object);
    return row_object;
}

static bool settings_page_build_content(settings_page_t *page)
{
    uint8_t row_index;

    page->content = lv_obj_create(page->root);
    if(page->content == NULL) return false;
    settings_page_make_plain(page->content);
    settings_page_add_key_handler(page, page->content);
    lv_obj_set_size(page->content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(page->content, page->theme.background, 0);
    lv_obj_set_style_bg_opa(
        page->content,
        settings_page_is_product(page) ? LV_OPA_TRANSP : LV_OPA_COVER,
        0);
    lv_obj_set_style_pad_top(
        page->content, (int32_t)page->header_height + 16, 0);
    lv_obj_set_style_pad_bottom(page->content, 20, 0);
    lv_obj_set_style_pad_left(page->content, 24, 0);
    lv_obj_set_style_pad_right(page->content, 24, 0);
    lv_obj_set_style_pad_row(page->content, 12, 0);
    lv_obj_set_style_pad_column(page->content, 14, 0);
    lv_obj_set_scrollbar_mode(page->content, LV_SCROLLBAR_MODE_OFF);

    for(row_index = 0u; row_index < page->row_count; ++row_index) {
        page->bindings[row_index].page = page;
        page->bindings[row_index].row_index = row_index;
    }

    if(settings_page_is_product(page) &&
       page->variant != SETTINGS_PAGE_VARIANT_DEFAULT) {
        lv_obj_remove_flag(page->content, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(page->content, 0, 0);
        if(!settings_page_build_product_variant(page)) return false;
        for(row_index = 0u; row_index < page->row_count; ++row_index) {
            settings_page_update_row_visual(page, row_index);
        }
        return true;
    }

    if(settings_page_is_product(page) &&
       page->layout == SETTINGS_PAGE_LAYOUT_GRID) {
        lv_obj_remove_flag(page->content, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(page->content, 0, 0);
        for(row_index = 0u; row_index < page->row_count; ++row_index) {
            lv_obj_t *row_object =
                settings_page_create_product_overview_row(page, row_index);

            if(row_object == NULL) return false;
            page->row_objects[row_index] = row_object;
            settings_page_position_product_overview(page, row_object, row_index);
            lv_obj_add_event_cb(
                row_object,
                settings_page_on_row_clicked,
                LV_EVENT_CLICKED,
                &page->bindings[row_index]);
            lv_obj_add_event_cb(
                row_object,
                settings_page_on_row_long_pressed,
                LV_EVENT_LONG_PRESSED,
                &page->bindings[row_index]);
            lv_obj_add_event_cb(
                row_object,
                settings_page_on_row_press_lost,
                LV_EVENT_PRESS_LOST,
                &page->bindings[row_index]);
        }
        return true;
    }

    if(settings_page_is_product(page) &&
       page->layout == SETTINGS_PAGE_LAYOUT_LIST) {
        lv_obj_set_size(page->content, 600, 380);
        lv_obj_set_pos(page->content, 100, 70);
        lv_obj_set_style_pad_all(page->content, 0, 0);
        lv_obj_set_style_pad_row(page->content, 8, 0);
    }

    if(page->layout == SETTINGS_PAGE_LAYOUT_GRID) {
        uint8_t grid_row_count =
            (uint8_t)((page->row_count + SETTINGS_PAGE_GRID_COLUMN_COUNT - 1u) /
                      SETTINGS_PAGE_GRID_COLUMN_COUNT);

        page->grid_columns[0] = LV_GRID_FR(1);
        page->grid_columns[1] = LV_GRID_FR(1);
        page->grid_columns[2] = LV_GRID_FR(1);
        page->grid_columns[3] = LV_GRID_TEMPLATE_LAST;
        for(uint8_t index = 0u; index < grid_row_count; ++index) {
            page->grid_rows[index] = LV_GRID_FR(1);
        }
        page->grid_rows[grid_row_count] = LV_GRID_TEMPLATE_LAST;
        lv_obj_set_grid_dsc_array(
            page->content, page->grid_columns, page->grid_rows);
        lv_obj_remove_flag(page->content, LV_OBJ_FLAG_SCROLLABLE);
        for(row_index = 0u; row_index < page->row_count; ++row_index) {
            lv_obj_t *row_object =
                settings_page_create_grid_row(page, row_index);

            page->row_objects[row_index] = row_object;
            lv_obj_set_grid_cell(
                row_object,
                LV_GRID_ALIGN_STRETCH,
                row_index % SETTINGS_PAGE_GRID_COLUMN_COUNT,
                1,
                LV_GRID_ALIGN_STRETCH,
                row_index / SETTINGS_PAGE_GRID_COLUMN_COUNT,
                1);
        }
    }
    else {
        const char *previous_section = NULL;

        lv_obj_add_flag(page->content, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(page->content, LV_FLEX_FLOW_COLUMN);
        for(row_index = 0u; row_index < page->row_count; ++row_index) {
            const settings_page_row_t *row = &page->rows[row_index];

            if(row->section != NULL &&
               (previous_section == NULL ||
                strcmp(previous_section, row->section) != 0)) {
                lv_obj_t *section = lv_obj_create(page->content);
                lv_obj_t *section_title;

                if(section == NULL) return false;
                settings_page_make_plain(section);
                lv_obj_set_width(section, LV_PCT(100));
                lv_obj_set_height(
                    section,
                    row->section_subtitle != NULL &&
                            row->section_subtitle[0] != '\0'
                        ? SETTINGS_PAGE_SECTION_HEIGHT * 2
                        : SETTINGS_PAGE_SECTION_HEIGHT);
                section_title = lv_label_create(section);
                if(section_title == NULL) return false;
                lv_label_set_text(section_title, row->section);
                lv_obj_set_width(section_title, LV_PCT(100));
                lv_obj_align(section_title, LV_ALIGN_TOP_LEFT, 0, 0);
                lv_obj_set_style_text_color(
                    section_title, page->theme.muted_text, 0);
                lv_obj_set_style_text_font(
                    section_title,
                    settings_page_font_or_default(page->body_font),
                    0);
                if(row->section_subtitle != NULL &&
                   row->section_subtitle[0] != '\0') {
                    lv_obj_t *subtitle = lv_label_create(section);

                    if(subtitle == NULL) return false;
                    lv_label_set_text(subtitle, row->section_subtitle);
                    lv_obj_set_width(subtitle, LV_PCT(100));
                    lv_obj_align(subtitle, LV_ALIGN_BOTTOM_LEFT, 0, 0);
                    lv_obj_set_style_text_color(
                        subtitle, page->theme.muted_text, 0);
                    lv_obj_set_style_text_font(
                        subtitle,
                        settings_page_font_or_default(
                            page->section_font != NULL
                                ? page->section_font
                                : page->body_font),
                        0);
                }
                previous_section = row->section;
            }
            lv_obj_t *row_object = settings_page_create_list_row(page, row_index);

            page->row_objects[row_index] = row_object;
        }
    }
    for(row_index = 0u; row_index < page->row_count; ++row_index) {
        settings_page_add_key_handler(
            page, page->row_objects[row_index]);
        lv_obj_add_event_cb(
            page->row_objects[row_index],
            settings_page_on_row_clicked,
            LV_EVENT_CLICKED,
            &page->bindings[row_index]);
        lv_obj_add_event_cb(
            page->row_objects[row_index],
            settings_page_on_row_long_pressed,
            LV_EVENT_LONG_PRESSED,
            &page->bindings[row_index]);
        lv_obj_add_event_cb(
            page->row_objects[row_index],
            settings_page_on_row_press_lost,
            LV_EVENT_PRESS_LOST,
            &page->bindings[row_index]);
        settings_page_update_row_visual(page, row_index);
    }
    return true;
}

static bool settings_page_build_header(settings_page_t *page)
{
    if(settings_page_is_product(page)) {
        page->title_label = lv_label_create(page->root);
        if(page->title_label == NULL) return false;
        lv_label_set_text(
            page->title_label, page->title != NULL ? page->title : "设置");
        lv_obj_set_size(page->title_label, 220, LV_SIZE_CONTENT);
        lv_obj_set_pos(page->title_label, 290, 18);
        lv_obj_set_style_text_color(
            page->title_label, lv_color_hex(SETTINGS_PAGE_PRODUCT_TITLE_COLOR), 0);
        lv_obj_set_style_text_font(
            page->title_label, settings_page_font_or_default(page->title_font), 0);
        lv_obj_set_style_text_align(page->title_label, LV_TEXT_ALIGN_CENTER, 0);

        if(page->show_back) {
            if(settings_page_asset_back(page) != NULL) {
                page->back_button = settings_page_create_image(
                    page->root, settings_page_asset_back(page), 100u, 100u);
            }
            else {
                page->back_button = lv_button_create(page->root);
                settings_page_make_plain(page->back_button);
                lv_obj_set_style_text_color(page->back_button, page->theme.text, 0);
                lv_label_set_text(
                    lv_label_create(page->back_button), LV_SYMBOL_LEFT);
            }
            if(page->back_button == NULL) return false;
            lv_obj_set_size(page->back_button, 100, 100);
            lv_obj_set_pos(page->back_button, 7, -2);
            lv_obj_add_flag(page->back_button, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_transform_scale(
                page->back_button, 270, LV_PART_MAIN | LV_STATE_PRESSED);
            lv_obj_set_style_transform_pivot_x(
                page->back_button, 50, LV_PART_MAIN | LV_STATE_PRESSED);
            lv_obj_set_style_transform_pivot_y(
                page->back_button, 50, LV_PART_MAIN | LV_STATE_PRESSED);
            lv_obj_add_event_cb(
                page->back_button,
                settings_page_on_back_clicked,
                LV_EVENT_CLICKED,
                page);
            settings_page_add_key_handler(page, page->back_button);
        }
        lv_obj_move_foreground(page->title_label);
        if(page->back_button != NULL) lv_obj_move_foreground(page->back_button);
        return true;
    }

    page->header = lv_obj_create(page->root);
    if(page->header == NULL) return false;
    settings_page_make_plain(page->header);
    settings_page_add_key_handler(page, page->header);
    lv_obj_set_size(page->header, LV_PCT(100), page->header_height);
    lv_obj_align(page->header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(page->header, page->theme.panel, 0);
    lv_obj_set_style_bg_opa(page->header, LV_OPA_COVER, 0);

    page->title_label = lv_label_create(page->header);
    lv_label_set_text(
        page->title_label, page->title != NULL ? page->title : "设置");
    lv_obj_set_style_text_color(page->title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(
        page->title_label, settings_page_font_or_default(page->title_font), 0);
    lv_obj_set_style_text_align(page->title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(page->title_label, LV_PCT(100));
    lv_obj_align(page->title_label, LV_ALIGN_CENTER, 0, 0);

    if(page->show_back) {
        page->back_button = lv_button_create(page->header);
        if(page->back_button == NULL) return false;
        lv_obj_set_size(page->back_button, 104, 44);
        lv_obj_align(page->back_button, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_set_style_radius(page->back_button, 10, 0);
        lv_obj_set_style_bg_color(
            page->back_button, page->theme.background, 0);
        lv_obj_set_style_bg_opa(page->back_button, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(
            page->back_button,
            settings_page_on_back_clicked,
            LV_EVENT_CLICKED,
            page);
        settings_page_add_key_handler(page, page->back_button);
        lv_obj_t *back_label = lv_label_create(page->back_button);

        lv_label_set_text(back_label, LV_SYMBOL_LEFT " 返回");
        lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(
            back_label, settings_page_font_or_default(page->body_font), 0);
        lv_obj_center(back_label);
    }
    lv_obj_move_foreground(page->header);
    return true;
}

static void settings_page_on_root_delete(lv_event_t *event)
{
    settings_page_t *page = lv_event_get_user_data(event);

    if(page == NULL) return;
    page->root = NULL;
    lv_free(page);
}

static void settings_page_on_key(lv_event_t *event)
{
    settings_page_t *page = lv_event_get_user_data(event);

    if(page != NULL && page->show_back &&
       lv_event_get_key(event) == LV_KEY_ESC) {
        settings_page_emit(
            page, SETTINGS_PAGE_EVENT_BACK, page->row_count, 0, 0u);
    }
}

static bool settings_page_validate_rows(
    const settings_page_t *page,
    settings_page_row_t *rows,
    uint8_t row_count)
{
    if(row_count > SETTINGS_PAGE_MAX_ROWS ||
       (row_count > 0u && rows == NULL)) {
        return false;
    }
    for(uint8_t row_index = 0u; row_index < row_count; ++row_index) {
        settings_page_row_t *row = &rows[row_index];

        if(row->id == 0u || row->kind > SETTINGS_PAGE_ROW_CHOICE ||
           row->accent > SETTINGS_PAGE_ACCENT_ORANGE ||
           row->wifi_status > SETTINGS_PAGE_WIFI_STATUS_ERROR) {
            return false;
        }
        for(uint8_t previous = 0u; previous < row_index; ++previous) {
            if(rows[previous].id == row->id) return false;
        }
        if(page->layout == SETTINGS_PAGE_LAYOUT_GRID &&
           row->kind != SETTINGS_PAGE_ROW_NAVIGATION &&
           row->kind != SETTINGS_PAGE_ROW_ACTION) {
            return false;
        }
        if(row->kind == SETTINGS_PAGE_ROW_SLIDER) {
            if(row->min_value == 0 && row->max_value == 0) {
                row->max_value = 100;
            }
            else if(row->max_value <= row->min_value) {
                return false;
            }
            if(row->step <= 0) row->step = 1;
            row->value = settings_page_snap_value(row, row->value);
        }
        if(row->kind == SETTINGS_PAGE_ROW_CHOICE) {
            if(row->options == NULL || row->option_count == 0u ||
               row->option_count > SETTINGS_PAGE_MAX_OPTIONS) {
                return false;
            }
            if(row->selected_option >= row->option_count) {
                row->selected_option = 0u;
            }
        }
    }
    return true;
}

static bool settings_page_copy_config(
    settings_page_t *page, const settings_page_config_t *config)
{
    if(config == NULL || config->row_count > SETTINGS_PAGE_MAX_ROWS ||
       (config->row_count > 0u && config->rows == NULL) ||
       config->layout > SETTINGS_PAGE_LAYOUT_LIST ||
       config->style > SETTINGS_PAGE_STYLE_UNIGUI ||
       config->variant > SETTINGS_PAGE_VARIANT_UNIGUI_UPGRADE ||
       (config->style != SETTINGS_PAGE_STYLE_UNIGUI &&
        config->variant != SETTINGS_PAGE_VARIANT_DEFAULT)) {
        return false;
    }
    page->title = config->title;
    page->layout = config->layout;
    page->row_count = config->row_count;
    page->show_back = config->show_back;
    page->header_height = config->header_height != 0u
                              ? config->header_height
                              : SETTINGS_PAGE_DEFAULT_HEADER_HEIGHT;
    page->symbol_font = config->symbol_font;
    page->title_font = config->title_font;
    page->overview_font = config->overview_font;
    page->emphasis_font = config->emphasis_font;
    page->body_font = config->body_font;
    page->section_font = config->section_font;
    page->on_event = config->on_event;
    page->user_ctx = config->user_ctx;
    page->style = config->style;
    page->variant = config->variant;
    if(config->assets != NULL) page->assets = *config->assets;
    settings_page_set_default_theme(&page->theme);
    if(config->theme != NULL) page->theme = *config->theme;
    if(settings_page_is_product(page) && config->theme == NULL) {
        settings_page_set_product_theme(&page->theme);
    }
    if(page->row_count > 0u) {
        memcpy(
            page->rows,
            config->rows,
            (size_t)page->row_count * sizeof(page->rows[0]));
    }
    if(!settings_page_validate_rows(page, page->rows, page->row_count)) {
        return false;
    }
    settings_page_sync_wifi_enabled(page);
    return true;
}

settings_page_t *settings_page_create(
    lv_obj_t *parent, const settings_page_config_t *config)
{
    settings_page_t *page;

    if(parent == NULL) return NULL;
    page = lv_malloc_zeroed(sizeof(*page));
    if(page == NULL) return NULL;
    if(!settings_page_copy_config(page, config)) {
        lv_free(page);
        return NULL;
    }
    page->root = lv_obj_create(parent);
    if(page->root == NULL) {
        lv_free(page);
        return NULL;
    }
    settings_page_make_plain(page->root);
    lv_obj_set_size(
        page->root,
        LV_PCT(100),
        settings_page_is_product(page) &&
                page->variant == SETTINGS_PAGE_VARIANT_UNIGUI_STORAGE
            ? SETTINGS_PAGE_PRODUCT_STORAGE_HEIGHT
            : LV_PCT(100));
    lv_obj_set_style_bg_color(page->root, page->theme.background, 0);
    lv_obj_set_style_bg_opa(page->root, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(
        page->root, settings_page_on_root_delete, LV_EVENT_DELETE, page);
    settings_page_add_key_handler(page, page->root);
    if(!settings_page_build_product_background(page) ||
       !settings_page_build_content(page) ||
       !settings_page_build_header(page)) {
        lv_obj_delete(page->root);
        return NULL;
    }
    return page;
}

void settings_page_destroy(settings_page_t *page)
{
    if(page == NULL) return;
    if(page->root != NULL) lv_obj_delete(page->root);
}

lv_obj_t *settings_page_root(settings_page_t *page)
{
    return page != NULL ? page->root : NULL;
}

void settings_page_set_title(settings_page_t *page, const char *title)
{
    if(page == NULL) return;
    page->title = title;
    if(page->title_label != NULL) {
        lv_label_set_text(
            page->title_label, title != NULL ? title : "设置");
    }
}

static void settings_page_reset_render_state(settings_page_t *page)
{
    bool wifi_loading = page->wifi_loading;

    page->content = NULL;
    page->decrement_button = NULL;
    page->increment_button = NULL;
    page->updating = false;
    memset(page->row_objects, 0, sizeof(page->row_objects));
    memset(page->value_labels, 0, sizeof(page->value_labels));
    memset(page->switch_objects, 0, sizeof(page->switch_objects));
    memset(page->slider_objects, 0, sizeof(page->slider_objects));
    memset(page->choice_objects, 0, sizeof(page->choice_objects));
    memset(
        page->choice_option_objects,
        0,
        sizeof(page->choice_option_objects));
    memset(page->wifi_connected_objects, 0, sizeof(page->wifi_connected_objects));
    memset(page->wifi_encrypted_objects, 0, sizeof(page->wifi_encrypted_objects));
    memset(page->wifi_signal_objects, 0, sizeof(page->wifi_signal_objects));
    memset(page->wifi_status_labels, 0, sizeof(page->wifi_status_labels));
    memset(page->wifi_current, 0, sizeof(page->wifi_current));
    memset(page->click_counts, 0, sizeof(page->click_counts));
    memset(page->click_times, 0, sizeof(page->click_times));
    memset(page->suppress_click, 0, sizeof(page->suppress_click));
    page->wifi_loading_object = NULL;
    page->wifi_loading = wifi_loading;
    memset(page->bindings, 0, sizeof(page->bindings));
    memset(page->option_bindings, 0, sizeof(page->option_bindings));
    memset(page->grid_columns, 0, sizeof(page->grid_columns));
    memset(page->grid_rows, 0, sizeof(page->grid_rows));
}

bool settings_page_set_rows(
    settings_page_t *page, const settings_page_row_t *rows, uint8_t row_count)
{
    settings_page_t previous;
    settings_page_row_t prepared[SETTINGS_PAGE_MAX_ROWS] = {0};

    if(page == NULL || page->root == NULL ||
       row_count > SETTINGS_PAGE_MAX_ROWS ||
       (row_count > 0u && rows == NULL)) {
        return false;
    }
    if(row_count > 0u) {
        memcpy(prepared, rows, (size_t)row_count * sizeof(prepared[0]));
    }
    if(!settings_page_validate_rows(page, prepared, row_count)) return false;

    previous = *page;
    memset(page->rows, 0, sizeof(page->rows));
    if(row_count > 0u) {
        memcpy(
            page->rows,
            prepared,
            (size_t)row_count * sizeof(page->rows[0]));
    }
    page->row_count = row_count;
    settings_page_sync_wifi_enabled(page);
    settings_page_reset_render_state(page);
    if(!settings_page_build_content(page)) {
        if(page->content != NULL) lv_obj_delete(page->content);
        *page = previous;
        return false;
    }

    if(previous.content != NULL) lv_obj_delete(previous.content);
    if(page->header != NULL) lv_obj_move_foreground(page->header);
    if(page->title_label != NULL) lv_obj_move_foreground(page->title_label);
    if(page->back_button != NULL) lv_obj_move_foreground(page->back_button);
    return true;
}

bool settings_page_set_value(
    settings_page_t *page, uint16_t row_id, int32_t value)
{
    int8_t row_index;

    if(page == NULL) return false;
    row_index = settings_page_find_row(page, row_id);
    if(row_index < 0 ||
       (page->rows[(uint8_t)row_index].kind != SETTINGS_PAGE_ROW_SLIDER &&
        !(page->variant == SETTINGS_PAGE_VARIANT_UNIGUI_STORAGE &&
          row_index == 0 &&
          page->rows[(uint8_t)row_index].kind == SETTINGS_PAGE_ROW_VALUE))) {
        return false;
    }
    if(page->rows[(uint8_t)row_index].kind == SETTINGS_PAGE_ROW_SLIDER) {
        page->rows[(uint8_t)row_index].value = settings_page_snap_value(
            &page->rows[(uint8_t)row_index], value);
    }
    else {
        page->rows[(uint8_t)row_index].value = value;
    }
    settings_page_update_row_visual(page, (uint8_t)row_index);
    return true;
}

bool settings_page_set_checked(
    settings_page_t *page, uint16_t row_id, bool checked)
{
    int8_t row_index;

    if(page == NULL) return false;
    row_index = settings_page_find_row(page, row_id);
    if(row_index < 0 ||
       page->rows[(uint8_t)row_index].kind != SETTINGS_PAGE_ROW_SWITCH) {
        return false;
    }
    page->rows[(uint8_t)row_index].checked = checked;
    if(page->variant == SETTINGS_PAGE_VARIANT_UNIGUI_WIFI) {
        page->wifi_enabled = checked;
        for(uint8_t index = 0u; index < page->row_count; ++index) {
            settings_page_update_row_visual(page, index);
        }
    }
    else {
        settings_page_update_row_visual(page, (uint8_t)row_index);
    }
    return true;
}

bool settings_page_set_choice(
    settings_page_t *page, uint16_t row_id, uint8_t option_index)
{
    int8_t row_index;

    if(page == NULL) return false;
    row_index = settings_page_find_row(page, row_id);
    if(row_index < 0 ||
       page->rows[(uint8_t)row_index].kind != SETTINGS_PAGE_ROW_CHOICE ||
       option_index >= page->rows[(uint8_t)row_index].option_count) {
        return false;
    }
    page->rows[(uint8_t)row_index].selected_option = option_index;
    settings_page_update_row_visual(page, (uint8_t)row_index);
    return true;
}

bool settings_page_set_value_text(
    settings_page_t *page, uint16_t row_id, const char *value_text)
{
    int8_t row_index;

    if(page == NULL) return false;
    row_index = settings_page_find_row(page, row_id);
    if(row_index < 0 ||
       (page->rows[(uint8_t)row_index].kind != SETTINGS_PAGE_ROW_VALUE &&
        page->rows[(uint8_t)row_index].kind !=
            SETTINGS_PAGE_ROW_NAVIGATION &&
        page->rows[(uint8_t)row_index].kind != SETTINGS_PAGE_ROW_ACTION)) {
        return false;
    }
    page->rows[(uint8_t)row_index].value_text = value_text;
    settings_page_update_row_visual(page, (uint8_t)row_index);
    return true;
}

bool settings_page_set_enabled(
    settings_page_t *page, uint16_t row_id, bool enabled)
{
    int8_t row_index;

    if(page == NULL) return false;
    row_index = settings_page_find_row(page, row_id);
    if(row_index < 0) return false;
    page->rows[(uint8_t)row_index].disabled = !enabled;
    settings_page_update_row_visual(page, (uint8_t)row_index);
    return true;
}

bool settings_page_set_wifi_state(
    settings_page_t *page,
    uint16_t row_id,
    const settings_page_wifi_state_t *state)
{
    int8_t row_index;
    settings_page_row_t *row;

    if(page == NULL || state == NULL ||
       page->variant != SETTINGS_PAGE_VARIANT_UNIGUI_WIFI ||
       state->status > SETTINGS_PAGE_WIFI_STATUS_ERROR) {
        return false;
    }
    row_index = settings_page_find_row(page, row_id);
    if(row_index < 0) return false;
    row = &page->rows[(uint8_t)row_index];
    if(row->kind != SETTINGS_PAGE_ROW_NAVIGATION &&
       row->kind != SETTINGS_PAGE_ROW_ACTION &&
       row->kind != SETTINGS_PAGE_ROW_VALUE) {
        return false;
    }
    row->wifi_encrypted = state->encrypted;
    row->wifi_remembered = state->remembered;
    row->wifi_signal_strength = state->signal_strength;
    row->wifi_status = state->status;
    settings_page_update_row_visual(page, (uint8_t)row_index);
    return true;
}

bool settings_page_set_wifi_loading(settings_page_t *page, bool loading)
{
    if(page == NULL || page->variant != SETTINGS_PAGE_VARIANT_UNIGUI_WIFI) {
        return false;
    }
    page->wifi_loading = loading;
    if(page->wifi_loading_object != NULL) {
        lv_obj_set_flag(
            page->wifi_loading_object, LV_OBJ_FLAG_HIDDEN, !loading);
        if(loading) lv_obj_move_foreground(page->wifi_loading_object);
    }
    return true;
}

lv_obj_t *settings_page_row_object(
    settings_page_t *page, uint16_t row_id)
{
    int8_t row_index;

    if(page == NULL) return NULL;
    row_index = settings_page_find_row(page, row_id);
    if(row_index < 0) return NULL;
    return page->row_objects[(uint8_t)row_index];
}
