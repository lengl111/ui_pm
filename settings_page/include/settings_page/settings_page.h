#ifndef SETTINGS_PAGE_SETTINGS_PAGE_H
#define SETTINGS_PAGE_SETTINGS_PAGE_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_PAGE_MAX_ROWS 32u
#define SETTINGS_PAGE_MAX_OPTIONS 8u

typedef struct settings_page settings_page_t;

typedef enum {
    SETTINGS_PAGE_LAYOUT_GRID = 0,
    SETTINGS_PAGE_LAYOUT_LIST,
} settings_page_layout_t;

typedef enum {
    SETTINGS_PAGE_STYLE_DEFAULT = 0,
    SETTINGS_PAGE_STYLE_UNIGUI,
} settings_page_style_t;

typedef enum {
    SETTINGS_PAGE_VARIANT_DEFAULT = 0,
    SETTINGS_PAGE_VARIANT_UNIGUI_BRIGHTNESS,
    SETTINGS_PAGE_VARIANT_UNIGUI_VOLUME,
    SETTINGS_PAGE_VARIANT_UNIGUI_ABOUT,
    SETTINGS_PAGE_VARIANT_UNIGUI_STORAGE,
    SETTINGS_PAGE_VARIANT_UNIGUI_ABOUT_DEVICE,
    SETTINGS_PAGE_VARIANT_UNIGUI_MOBILE_NETWORK,
    SETTINGS_PAGE_VARIANT_UNIGUI_WIFI,
    SETTINGS_PAGE_VARIANT_UNIGUI_UPGRADE,
} settings_page_variant_t;

typedef enum {
    SETTINGS_PAGE_ROW_NAVIGATION = 0,
    SETTINGS_PAGE_ROW_ACTION,
    SETTINGS_PAGE_ROW_VALUE,
    SETTINGS_PAGE_ROW_SWITCH,
    SETTINGS_PAGE_ROW_SLIDER,
    SETTINGS_PAGE_ROW_CHOICE,
} settings_page_row_kind_t;

typedef enum {
    SETTINGS_PAGE_ACCENT_YELLOW = 0,
    SETTINGS_PAGE_ACCENT_BLUE,
    SETTINGS_PAGE_ACCENT_PINK,
    SETTINGS_PAGE_ACCENT_PURPLE,
    SETTINGS_PAGE_ACCENT_GREEN,
    SETTINGS_PAGE_ACCENT_ORANGE,
} settings_page_accent_t;

typedef enum {
    SETTINGS_PAGE_EVENT_BACK = 0,
    SETTINGS_PAGE_EVENT_ROW_ACTIVATED,
    SETTINGS_PAGE_EVENT_SWITCH_CHANGED,
    SETTINGS_PAGE_EVENT_SLIDER_CHANGED,
    SETTINGS_PAGE_EVENT_CHOICE_CHANGED,
    SETTINGS_PAGE_EVENT_ROW_LONG_PRESSED,
    SETTINGS_PAGE_EVENT_SLIDER_RELEASED,
} settings_page_event_kind_t;

typedef enum {
    SETTINGS_PAGE_WIFI_STATUS_NONE = 0,
    SETTINGS_PAGE_WIFI_STATUS_CONNECTED,
    SETTINGS_PAGE_WIFI_STATUS_CONNECTING,
    SETTINGS_PAGE_WIFI_STATUS_ERROR,
} settings_page_wifi_status_t;

typedef struct {
    uint16_t id;
    const char *section;
    const char *title;
    const char *subtitle;
    const char *symbol;
    settings_page_row_kind_t kind;
    settings_page_accent_t accent;
    bool disabled;
    int32_t value;
    int32_t min_value;
    int32_t max_value;
    int32_t step;
    bool checked;
    uint8_t selected_option;
    const char *const *options;
    uint8_t option_count;
    const char *value_text;
    const void *image_src;
    const char *section_subtitle;
    bool wifi_encrypted;
    bool wifi_remembered;
    int16_t wifi_signal_strength;
    settings_page_wifi_status_t wifi_status;
    uint8_t activation_clicks;
    uint16_t activation_window_ms;
} settings_page_row_t;

typedef struct {
    const void *background_src;
    const void *title_background_src;
    const void *back_src;
    const void *arrow_src;
    const void *slider_knob_src;
    const void *decrement_src;
    const void *increment_src;
    const lv_point_t *overview_positions;
    uint8_t overview_position_count;
    uint16_t overview_item_width;
    uint16_t overview_item_height;
    uint16_t overview_icon_width;
    uint16_t overview_icon_height;
    uint16_t overview_label_gap;
    int32_t decrement_step;
    int32_t increment_step;
    const void *wifi_connected_src;
    const void *wifi_encrypted_src;
    const void *wifi_signal_weak_src;
    const void *wifi_signal_medium_src;
    const void *wifi_signal_strong_src;
} settings_page_assets_t;

typedef struct {
    lv_color_t background;
    lv_color_t panel;
    lv_color_t row;
    lv_color_t accent;
    lv_color_t text;
    lv_color_t muted_text;
} settings_page_theme_t;

typedef struct {
    settings_page_event_kind_t kind;
    uint16_t row_id;
    int32_t value;
    uint8_t option_index;
    uint8_t click_count;
} settings_page_event_t;

typedef struct {
    bool encrypted;
    bool remembered;
    int16_t signal_strength;
    settings_page_wifi_status_t status;
} settings_page_wifi_state_t;

typedef void (*settings_page_event_fn)(
    void *user_ctx, const settings_page_event_t *event);

typedef struct {
    const char *title;
    settings_page_layout_t layout;
    const settings_page_row_t *rows;
    uint8_t row_count;
    bool show_back;
    uint16_t header_height;
    const lv_font_t *symbol_font;
    const lv_font_t *title_font;
    const lv_font_t *overview_font;
    const lv_font_t *body_font;
    const settings_page_theme_t *theme;
    settings_page_event_fn on_event;
    void *user_ctx;
    settings_page_style_t style;
    settings_page_variant_t variant;
    const settings_page_assets_t *assets;
    const lv_font_t *emphasis_font;
    const lv_font_t *section_font;
} settings_page_config_t;

settings_page_t *settings_page_create(
    lv_obj_t *parent, const settings_page_config_t *config);
void settings_page_destroy(settings_page_t *page);

lv_obj_t *settings_page_root(settings_page_t *page);
void settings_page_set_title(settings_page_t *page, const char *title);

bool settings_page_set_value(
    settings_page_t *page, uint16_t row_id, int32_t value);
bool settings_page_set_checked(
    settings_page_t *page, uint16_t row_id, bool checked);
bool settings_page_set_choice(
    settings_page_t *page, uint16_t row_id, uint8_t option_index);
bool settings_page_set_value_text(
    settings_page_t *page, uint16_t row_id, const char *value_text);
bool settings_page_set_enabled(
    settings_page_t *page, uint16_t row_id, bool enabled);
bool settings_page_set_rows(
    settings_page_t *page, const settings_page_row_t *rows, uint8_t row_count);
bool settings_page_set_wifi_state(
    settings_page_t *page,
    uint16_t row_id,
    const settings_page_wifi_state_t *state);
bool settings_page_set_wifi_loading(settings_page_t *page, bool loading);
lv_obj_t *settings_page_row_object(
    settings_page_t *page, uint16_t row_id);

#ifdef __cplusplus
}
#endif

#endif
