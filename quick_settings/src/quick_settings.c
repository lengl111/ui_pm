#include "quick_settings/quick_settings.h"

#include <stddef.h>

#define QUICK_SETTINGS_DEFAULT_PANEL_HEIGHT 340
#define QUICK_SETTINGS_DEFAULT_EDGE_HEIGHT 56
#define QUICK_SETTINGS_DEFAULT_OPEN_DISTANCE 96
#define QUICK_SETTINGS_DEFAULT_CLOSE_DISTANCE 72
#define QUICK_SETTINGS_DEFAULT_ANIMATION_MS 240
#define QUICK_SETTINGS_WIFI_NAME_MAX 63u
#define QUICK_SETTINGS_GESTURE_VERTICAL_SLOP 8
#define QUICK_SETTINGS_GESTURE_HORIZONTAL_SLOP 24

typedef struct {
    uint8_t id;
    int32_t start_x;
    int32_t start_y;
    int32_t x;
    int32_t y;
    bool active;
    bool path_valid;
} quick_settings_finger_t;

struct quick_settings {
    lv_obj_t *root;
    lv_obj_t *backdrop;
    lv_obj_t *panel;
    lv_obj_t *wifi_button;
    lv_obj_t *wifi_symbol;
    lv_obj_t *wifi_title;
    lv_obj_t *wifi_status;
    lv_obj_t *brightness_slider;
    lv_obj_t *brightness_value;
    lv_obj_t *volume_slider;
    lv_obj_t *volume_value;
    quick_settings_finger_t fingers[2];
    quick_settings_event_fn on_event;
    void *user_ctx;
    const lv_font_t *title_font;
    const lv_font_t *body_font;
    const lv_font_t *symbol_font;
    uint16_t panel_height;
    uint16_t edge_start_height;
    uint16_t open_distance;
    uint16_t close_distance;
    uint16_t animation_duration_ms;
    int32_t brightness;
    int32_t volume;
    char wifi_name[QUICK_SETTINGS_WIFI_NAME_MAX + 1u];
    bool wifi_enabled;
    bool is_open;
    bool gesture_tracking;
    bool gesture_eligible;
    bool gesture_claimed;
    bool gesture_opened;
    bool gesture_close_armed;
    int32_t close_start_y;
    int32_t close_current_y;
    bool suppress_controls;
};

static int32_t quick_settings_clamp_percent(int32_t value)
{
    if(value < 0) return 0;
    if(value > 100) return 100;
    return value;
}

static const lv_font_t *quick_settings_font_or_default(
    const lv_font_t *font)
{
    return font != NULL ? font : LV_FONT_DEFAULT;
}

static void quick_settings_make_plain(lv_obj_t *object)
{
    lv_obj_remove_style_all(object);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

static void quick_settings_copy_wifi_name(
    quick_settings_t *settings, const char *name)
{
    size_t index = 0u;

    if(name != NULL) {
        while(index < QUICK_SETTINGS_WIFI_NAME_MAX && name[index] != '\0') {
            settings->wifi_name[index] = name[index];
            ++index;
        }
    }
    settings->wifi_name[index] = '\0';
}

static void quick_settings_emit(
    quick_settings_t *settings,
    quick_settings_event_kind_t kind,
    int32_t value,
    bool enabled,
    quick_settings_close_reason_t reason)
{
    quick_settings_event_t event;

    if(settings->on_event == NULL) return;
    event.kind = kind;
    event.value = value;
    event.enabled = enabled;
    event.close_reason = reason;
    settings->on_event(settings->user_ctx, &event);
}

static void quick_settings_update_wifi_visual(quick_settings_t *settings)
{
    lv_color_t background = settings->wifi_enabled
                                ? lv_color_hex(0x1687F8)
                                : lv_color_hex(0x34383E);
    lv_color_t foreground = settings->wifi_enabled
                                ? lv_color_hex(0xFFFFFF)
                                : lv_color_hex(0xD0D4D9);

    if(settings->wifi_button == NULL) return;
    lv_obj_set_style_bg_color(settings->wifi_button, background, 0);
    lv_obj_set_style_text_color(settings->wifi_button, foreground, 0);
    lv_obj_set_style_border_opa(
        settings->wifi_button,
        settings->wifi_enabled ? LV_OPA_TRANSP : LV_OPA_20,
        0);
    lv_obj_set_style_text_color(settings->wifi_symbol, foreground, 0);
    lv_obj_set_style_text_color(settings->wifi_title, foreground, 0);
    lv_obj_set_style_text_color(
        settings->wifi_status,
        settings->wifi_enabled ? lv_color_hex(0xEAF5FF)
                               : lv_color_hex(0xA7ADB5),
        0);
    if(settings->wifi_enabled) {
        lv_label_set_text(
            settings->wifi_status,
            settings->wifi_name[0] != '\0'
                ? settings->wifi_name
                : "Connected");
    }
    else {
        lv_label_set_text(settings->wifi_status, "Off");
    }
}

static void quick_settings_update_value_label(
    lv_obj_t *label, int32_t value)
{
    if(label != NULL) lv_label_set_text_fmt(label, "%d%%", (int)value);
}

static void quick_settings_apply_progress(
    quick_settings_t *settings, int32_t progress)
{
    int32_t panel_y;
    int32_t backdrop_opacity;

    if(settings == NULL || settings->panel == NULL) return;
    progress = quick_settings_clamp_percent(progress);
    panel_y = -((int32_t)settings->panel_height) +
              (int32_t)settings->panel_height * progress / 100;
    backdrop_opacity = (int32_t)LV_OPA_60 * progress / 100;
    lv_obj_set_y(settings->panel, panel_y);
    lv_obj_set_style_bg_opa(
        settings->backdrop, (lv_opa_t)backdrop_opacity, 0);
}

static int32_t quick_settings_current_progress(
    const quick_settings_t *settings)
{
    int32_t panel_y;
    int32_t progress;

    if(settings == NULL || settings->panel == NULL ||
       settings->panel_height == 0u) {
        return 0;
    }
    panel_y = lv_obj_get_y(settings->panel);
    progress = (panel_y + (int32_t)settings->panel_height) * 100 /
               (int32_t)settings->panel_height;
    return quick_settings_clamp_percent(progress);
}

static void quick_settings_animation_exec(void *variable, int32_t value)
{
    quick_settings_apply_progress(variable, value);
}

static void quick_settings_hide_completed(lv_anim_t *animation)
{
    quick_settings_t *settings = lv_anim_get_user_data(animation);

    if(settings != NULL && settings->root != NULL && !settings->is_open) {
        lv_obj_add_flag(settings->root, LV_OBJ_FLAG_HIDDEN);
    }
}

static void quick_settings_start_animation(
    quick_settings_t *settings, int32_t target_progress)
{
    lv_anim_t animation;
    int32_t start_progress;

    if(settings == NULL || settings->root == NULL) return;
    start_progress = quick_settings_current_progress(settings);
    lv_anim_delete(settings, quick_settings_animation_exec);
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, settings);
    lv_anim_set_exec_cb(&animation, quick_settings_animation_exec);
    lv_anim_set_values(&animation, start_progress, target_progress);
    lv_anim_set_duration(&animation, settings->animation_duration_ms);
    lv_anim_set_path_cb(&animation, lv_anim_path_custom_bezier3);
    LV_ANIM_SET_EASE_OUT_CUBIC(&animation);
    lv_anim_set_user_data(&animation, settings);
    if(target_progress == 0) {
        lv_anim_set_completed_cb(&animation, quick_settings_hide_completed);
    }
    lv_anim_start(&animation);
}

static void quick_settings_on_root_delete(lv_event_t *event)
{
    quick_settings_t *settings = lv_event_get_user_data(event);

    lv_anim_delete(settings, quick_settings_animation_exec);
    settings->root = NULL;
    lv_free(settings);
}

static void quick_settings_on_backdrop_clicked(lv_event_t *event)
{
    quick_settings_t *settings = lv_event_get_user_data(event);

    quick_settings_hide(
        settings, LV_ANIM_ON, QUICK_SETTINGS_CLOSE_BACKDROP);
}

static void quick_settings_on_wifi_clicked(lv_event_t *event)
{
    quick_settings_t *settings = lv_event_get_user_data(event);

    settings->wifi_enabled = !settings->wifi_enabled;
    quick_settings_update_wifi_visual(settings);
    quick_settings_emit(
        settings,
        QUICK_SETTINGS_EVENT_WIFI_CHANGED,
        settings->wifi_enabled ? 1 : 0,
        settings->wifi_enabled,
        QUICK_SETTINGS_CLOSE_PROGRAMMATIC);
}

static void quick_settings_on_brightness_changed(lv_event_t *event)
{
    quick_settings_t *settings = lv_event_get_user_data(event);

    if(settings->suppress_controls) return;
    settings->brightness = lv_slider_get_value(settings->brightness_slider);
    quick_settings_update_value_label(
        settings->brightness_value, settings->brightness);
    quick_settings_emit(
        settings,
        QUICK_SETTINGS_EVENT_BRIGHTNESS_CHANGED,
        settings->brightness,
        false,
        QUICK_SETTINGS_CLOSE_PROGRAMMATIC);
}

static void quick_settings_on_volume_changed(lv_event_t *event)
{
    quick_settings_t *settings = lv_event_get_user_data(event);

    if(settings->suppress_controls) return;
    settings->volume = lv_slider_get_value(settings->volume_slider);
    quick_settings_update_value_label(
        settings->volume_value, settings->volume);
    quick_settings_emit(
        settings,
        QUICK_SETTINGS_EVENT_VOLUME_CHANGED,
        settings->volume,
        false,
        QUICK_SETTINGS_CLOSE_PROGRAMMATIC);
}

static void quick_settings_finish_panel_drag(quick_settings_t *settings)
{
    int32_t delta;

    if(settings == NULL || !settings->gesture_close_armed) return;
    settings->gesture_close_armed = false;
    delta = settings->close_current_y - settings->close_start_y;
    if(delta <= -(int32_t)settings->close_distance) {
        quick_settings_hide(
            settings, LV_ANIM_ON, QUICK_SETTINGS_CLOSE_SWIPE);
    }
    else {
        settings->is_open = true;
        quick_settings_start_animation(settings, 100);
    }
}

static void quick_settings_on_panel_pressing(lv_event_t *event)
{
    quick_settings_t *settings = lv_event_get_user_data(event);
    lv_indev_t *indev;
    lv_point_t point;
    int32_t delta;
    int32_t progress;

    indev = lv_event_get_indev(event);
    if(indev == NULL) return;
    lv_indev_get_point(indev, &point);
    if(!settings->gesture_close_armed) {
        settings->gesture_close_armed = true;
        settings->close_start_y = point.y;
        lv_anim_delete(settings, quick_settings_animation_exec);
    }
    settings->close_current_y = point.y;
    delta = point.y - settings->close_start_y;
    if(delta >= 0) return;
    progress = 100 + delta * 100 / (int32_t)settings->panel_height;
    quick_settings_apply_progress(settings, progress);
}

static void quick_settings_on_panel_released(lv_event_t *event)
{
    quick_settings_t *settings = lv_event_get_user_data(event);

    quick_settings_finish_panel_drag(settings);
}

static lv_obj_t *quick_settings_create_label(
    lv_obj_t *parent,
    const char *text,
    const lv_font_t *font,
    lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    if(label == NULL) return NULL;
    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_set_style_text_font(label, quick_settings_font_or_default(font), 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

static bool quick_settings_create_wifi_tile(quick_settings_t *settings)
{
    settings->wifi_button = lv_button_create(settings->panel);
    if(settings->wifi_button == NULL) return false;
    lv_obj_set_size(settings->wifi_button, 330, 104);
    lv_obj_set_pos(settings->wifi_button, 24, 94);
    lv_obj_set_style_radius(settings->wifi_button, 8, 0);
    lv_obj_set_style_border_width(settings->wifi_button, 1, 0);
    lv_obj_set_style_border_color(
        settings->wifi_button, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_shadow_width(settings->wifi_button, 0, 0);
    lv_obj_set_style_opa(settings->wifi_button, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_add_event_cb(
        settings->wifi_button,
        quick_settings_on_wifi_clicked,
        LV_EVENT_CLICKED,
        settings);

    settings->wifi_symbol = quick_settings_create_label(
        settings->wifi_button,
        LV_SYMBOL_WIFI,
        settings->symbol_font,
        lv_color_hex(0xFFFFFF));
    settings->wifi_title = quick_settings_create_label(
        settings->wifi_button,
        "Wi-Fi",
        settings->body_font,
        lv_color_hex(0xFFFFFF));
    settings->wifi_status = quick_settings_create_label(
        settings->wifi_button,
        "Connected",
        LV_FONT_DEFAULT,
        lv_color_hex(0xEAF5FF));
    if(settings->wifi_symbol == NULL || settings->wifi_title == NULL ||
       settings->wifi_status == NULL) {
        return false;
    }
    lv_obj_set_style_text_font(
        settings->wifi_symbol,
        quick_settings_font_or_default(settings->symbol_font),
        0);
    lv_obj_align(settings->wifi_symbol, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_align(settings->wifi_title, LV_ALIGN_TOP_LEFT, 68, 22);
    lv_obj_align(settings->wifi_status, LV_ALIGN_BOTTOM_LEFT, 68, -20);
    return true;
}

static bool quick_settings_create_info_tile(
    quick_settings_t *settings,
    int32_t x,
    const char *symbol_text,
    const char *title_text,
    const char *status_text)
{
    lv_obj_t *tile = lv_obj_create(settings->panel);
    lv_obj_t *symbol;
    lv_obj_t *title;
    lv_obj_t *status;

    if(tile == NULL) return false;
    quick_settings_make_plain(tile);
    lv_obj_set_size(tile, 196, 104);
    lv_obj_set_pos(tile, x, 94);
    lv_obj_set_style_radius(tile, 8, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x34383E), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(tile, LV_OPA_20, 0);

    symbol = quick_settings_create_label(
        tile, symbol_text, settings->symbol_font, lv_color_hex(0xD9DEE4));
    title = quick_settings_create_label(
        tile, title_text, settings->body_font, lv_color_hex(0xFFFFFF));
    status = quick_settings_create_label(
        tile, status_text, LV_FONT_DEFAULT, lv_color_hex(0xA7ADB5));
    if(symbol == NULL || title == NULL || status == NULL) return false;
    lv_obj_align(symbol, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 66, 22);
    lv_obj_align(status, LV_ALIGN_BOTTOM_LEFT, 66, -20);
    return true;
}

static void quick_settings_style_slider(lv_obj_t *slider)
{
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        slider, lv_color_hex(0x3D4249), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(
        slider, lv_color_hex(0x3FA8FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(
        slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_width(slider, 28, LV_PART_KNOB);
    lv_obj_set_style_height(slider, 28, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 6, LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(slider, LV_OPA_30, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, 12);
}

static bool quick_settings_create_slider_row(
    quick_settings_t *settings,
    int32_t y,
    const char *symbol_text,
    const char *title_text,
    lv_obj_t **slider_output,
    lv_obj_t **value_output,
    lv_event_cb_t event_callback)
{
    lv_obj_t *symbol;
    lv_obj_t *title;
    lv_obj_t *slider;
    lv_obj_t *value;

    symbol = quick_settings_create_label(
        settings->panel,
        symbol_text,
        settings->symbol_font,
        lv_color_hex(0xE5E9ED));
    title = quick_settings_create_label(
        settings->panel,
        title_text,
        settings->body_font,
        lv_color_hex(0xF5F7F9));
    value = quick_settings_create_label(
        settings->panel,
        "0%",
        settings->body_font,
        lv_color_hex(0xD4D8DD));
    slider = lv_slider_create(settings->panel);
    if(symbol == NULL || title == NULL || value == NULL || slider == NULL) {
        return false;
    }
    lv_obj_set_size(symbol, 42, 40);
    lv_obj_set_pos(symbol, 30, y + 15);
    lv_obj_set_style_text_align(symbol, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(title, 118, 34);
    lv_obj_set_pos(title, 83, y + 19);
    lv_obj_set_size(slider, 440, 18);
    lv_obj_set_pos(slider, 202, y + 25);
    lv_slider_set_range(slider, 0, 100);
    quick_settings_style_slider(slider);
    lv_obj_set_size(value, 76, 34);
    lv_obj_set_pos(value, 676, y + 19);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_event_cb(
        slider, event_callback, LV_EVENT_VALUE_CHANGED, settings);
    *slider_output = slider;
    *value_output = value;
    return true;
}

static bool quick_settings_build(
    quick_settings_t *settings, lv_obj_t *parent)
{
    lv_obj_t *title;
    lv_obj_t *subtitle;
    lv_obj_t *handle;

    settings->root = lv_obj_create(parent);
    if(settings->root == NULL) return false;
    quick_settings_make_plain(settings->root);
    lv_obj_set_size(settings->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(settings->root, 0, 0);
    lv_obj_add_flag(settings->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        settings->root,
        quick_settings_on_root_delete,
        LV_EVENT_DELETE,
        settings);

    settings->backdrop = lv_obj_create(settings->root);
    if(settings->backdrop == NULL) return false;
    quick_settings_make_plain(settings->backdrop);
    lv_obj_set_size(settings->backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(settings->backdrop, 0, 0);
    lv_obj_set_style_bg_color(
        settings->backdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(settings->backdrop, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(settings->backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        settings->backdrop,
        quick_settings_on_backdrop_clicked,
        LV_EVENT_CLICKED,
        settings);

    settings->panel = lv_obj_create(settings->root);
    if(settings->panel == NULL) return false;
    quick_settings_make_plain(settings->panel);
    lv_obj_set_size(settings->panel, LV_PCT(100), settings->panel_height);
    lv_obj_set_pos(settings->panel, 0, -(int32_t)settings->panel_height);
    lv_obj_set_style_radius(settings->panel, 0, 0);
    lv_obj_set_style_bg_color(settings->panel, lv_color_hex(0x202327), 0);
    lv_obj_set_style_bg_opa(settings->panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(settings->panel, 0, 0);
    lv_obj_set_style_shadow_width(settings->panel, 24, 0);
    lv_obj_set_style_shadow_offset_y(settings->panel, 8, 0);
    lv_obj_set_style_shadow_color(settings->panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(settings->panel, LV_OPA_40, 0);
    lv_obj_add_flag(settings->panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        settings->panel,
        quick_settings_on_panel_pressing,
        LV_EVENT_PRESSING,
        settings);
    lv_obj_add_event_cb(
        settings->panel,
        quick_settings_on_panel_released,
        LV_EVENT_RELEASED,
        settings);
    lv_obj_add_event_cb(
        settings->panel,
        quick_settings_on_panel_released,
        LV_EVENT_PRESS_LOST,
        settings);

    title = quick_settings_create_label(
        settings->panel,
        "Quick settings",
        settings->title_font,
        lv_color_hex(0xFFFFFF));
    subtitle = quick_settings_create_label(
        settings->panel,
        "12:00  |  Mon, Sep 7",
        settings->body_font,
        lv_color_hex(0xAEB4BC));
    if(title == NULL || subtitle == NULL) return false;
    lv_obj_set_pos(title, 24, 20);
    lv_obj_set_pos(subtitle, 24, 56);

    if(!quick_settings_create_wifi_tile(settings) ||
       !quick_settings_create_info_tile(
           settings, 366, LV_SYMBOL_BLUETOOTH, "Bluetooth", "Available") ||
       !quick_settings_create_info_tile(
           settings, 574, LV_SYMBOL_SETTINGS, "Device", "Controls")) {
        return false;
    }

    if(!quick_settings_create_slider_row(
           settings,
           210,
           LV_SYMBOL_EYE_OPEN,
           "Brightness",
           &settings->brightness_slider,
           &settings->brightness_value,
           quick_settings_on_brightness_changed) ||
       !quick_settings_create_slider_row(
           settings,
           270,
           LV_SYMBOL_VOLUME_MAX,
           "Volume",
           &settings->volume_slider,
           &settings->volume_value,
           quick_settings_on_volume_changed)) {
        return false;
    }

    handle = lv_obj_create(settings->panel);
    if(handle == NULL) return false;
    quick_settings_make_plain(handle);
    lv_obj_set_size(handle, 76, 5);
    lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_radius(handle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(handle, lv_color_hex(0x747A82), 0);
    lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, 0);

    lv_obj_add_flag(settings->root, LV_OBJ_FLAG_HIDDEN);
    return true;
}

quick_settings_t *quick_settings_create(
    lv_obj_t *parent, const quick_settings_config_t *config)
{
    quick_settings_t *settings;
    quick_settings_state_t initial_state = {
        .wifi_enabled = true,
        .brightness = 72,
        .volume = 46,
        .wifi_name = "Studio Wi-Fi",
    };

    if(parent == NULL) return NULL;
    settings = lv_malloc_zeroed(sizeof(*settings));
    if(settings == NULL) return NULL;
    settings->panel_height =
        config != NULL && config->panel_height > 0u
            ? config->panel_height
            : QUICK_SETTINGS_DEFAULT_PANEL_HEIGHT;
    settings->edge_start_height =
        config != NULL && config->edge_start_height > 0u
            ? config->edge_start_height
            : QUICK_SETTINGS_DEFAULT_EDGE_HEIGHT;
    settings->open_distance =
        config != NULL && config->open_distance > 0u
            ? config->open_distance
            : QUICK_SETTINGS_DEFAULT_OPEN_DISTANCE;
    settings->close_distance =
        config != NULL && config->close_distance > 0u
            ? config->close_distance
            : QUICK_SETTINGS_DEFAULT_CLOSE_DISTANCE;
    settings->animation_duration_ms =
        config != NULL && config->animation_duration_ms > 0u
            ? config->animation_duration_ms
            : QUICK_SETTINGS_DEFAULT_ANIMATION_MS;
    if(settings->panel_height < QUICK_SETTINGS_DEFAULT_PANEL_HEIGHT) {
        settings->panel_height = QUICK_SETTINGS_DEFAULT_PANEL_HEIGHT;
    }
    settings->title_font = config != NULL ? config->title_font : NULL;
    settings->body_font = config != NULL ? config->body_font : NULL;
    settings->symbol_font = config != NULL ? config->symbol_font : NULL;
    settings->on_event = config != NULL ? config->on_event : NULL;
    settings->user_ctx = config != NULL ? config->user_ctx : NULL;

    if(!quick_settings_build(settings, parent)) {
        if(settings->root != NULL) {
            lv_obj_remove_event_cb_with_user_data(
                settings->root, quick_settings_on_root_delete, settings);
            lv_obj_delete(settings->root);
        }
        lv_free(settings);
        return NULL;
    }
    quick_settings_set_state(settings, &initial_state);
    return settings;
}

void quick_settings_destroy(quick_settings_t *settings)
{
    if(settings == NULL) return;
    if(settings->root != NULL) {
        lv_obj_delete(settings->root);
    }
}

lv_obj_t *quick_settings_root(quick_settings_t *settings)
{
    return settings != NULL ? settings->root : NULL;
}

bool quick_settings_is_open(const quick_settings_t *settings)
{
    return settings != NULL && settings->is_open;
}

void quick_settings_show(
    quick_settings_t *settings, lv_anim_enable_t animation)
{
    bool changed;

    if(settings == NULL || settings->root == NULL) return;
    changed = !settings->is_open;
    settings->is_open = true;
    settings->gesture_close_armed = false;
    lv_obj_remove_flag(settings->root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(settings->root);
    if(animation) {
        quick_settings_start_animation(settings, 100);
    }
    else {
        lv_anim_delete(settings, quick_settings_animation_exec);
        quick_settings_apply_progress(settings, 100);
    }
    if(changed) {
        quick_settings_emit(
            settings,
            QUICK_SETTINGS_EVENT_OPENED,
            0,
            false,
            QUICK_SETTINGS_CLOSE_PROGRAMMATIC);
    }
}

void quick_settings_hide(
    quick_settings_t *settings,
    lv_anim_enable_t animation,
    quick_settings_close_reason_t reason)
{
    bool changed;

    if(settings == NULL || settings->root == NULL) return;
    changed = settings->is_open;
    settings->is_open = false;
    settings->gesture_close_armed = false;
    if(animation) {
        quick_settings_start_animation(settings, 0);
    }
    else {
        lv_anim_delete(settings, quick_settings_animation_exec);
        quick_settings_apply_progress(settings, 0);
        lv_obj_add_flag(settings->root, LV_OBJ_FLAG_HIDDEN);
    }
    if(changed) {
        quick_settings_emit(
            settings,
            QUICK_SETTINGS_EVENT_CLOSED,
            0,
            false,
            reason);
    }
}

void quick_settings_toggle(
    quick_settings_t *settings, lv_anim_enable_t animation)
{
    if(quick_settings_is_open(settings)) {
        quick_settings_hide(
            settings,
            animation,
            QUICK_SETTINGS_CLOSE_PROGRAMMATIC);
    }
    else {
        quick_settings_show(settings, animation);
    }
}

void quick_settings_set_state(
    quick_settings_t *settings, const quick_settings_state_t *state)
{
    if(settings == NULL || state == NULL) return;
    settings->suppress_controls = true;
    settings->wifi_enabled = state->wifi_enabled;
    settings->brightness = quick_settings_clamp_percent(state->brightness);
    settings->volume = quick_settings_clamp_percent(state->volume);
    quick_settings_copy_wifi_name(settings, state->wifi_name);
    lv_slider_set_value(
        settings->brightness_slider, settings->brightness, LV_ANIM_OFF);
    lv_slider_set_value(
        settings->volume_slider, settings->volume, LV_ANIM_OFF);
    quick_settings_update_value_label(
        settings->brightness_value, settings->brightness);
    quick_settings_update_value_label(
        settings->volume_value, settings->volume);
    quick_settings_update_wifi_visual(settings);
    settings->suppress_controls = false;
}

void quick_settings_get_state(
    const quick_settings_t *settings, quick_settings_state_t *state)
{
    if(settings == NULL || state == NULL) return;
    state->wifi_enabled = settings->wifi_enabled;
    state->brightness = settings->brightness;
    state->volume = settings->volume;
    state->wifi_name = settings->wifi_name;
}

void quick_settings_set_wifi(
    quick_settings_t *settings, bool enabled)
{
    if(settings == NULL) return;
    settings->wifi_enabled = enabled;
    quick_settings_update_wifi_visual(settings);
}

void quick_settings_set_brightness(
    quick_settings_t *settings, int32_t value)
{
    if(settings == NULL) return;
    settings->brightness = quick_settings_clamp_percent(value);
    settings->suppress_controls = true;
    lv_slider_set_value(
        settings->brightness_slider, settings->brightness, LV_ANIM_OFF);
    settings->suppress_controls = false;
    quick_settings_update_value_label(
        settings->brightness_value, settings->brightness);
}

void quick_settings_set_volume(
    quick_settings_t *settings, int32_t value)
{
    if(settings == NULL) return;
    settings->volume = quick_settings_clamp_percent(value);
    settings->suppress_controls = true;
    lv_slider_set_value(
        settings->volume_slider, settings->volume, LV_ANIM_OFF);
    settings->suppress_controls = false;
    quick_settings_update_value_label(
        settings->volume_value, settings->volume);
}

void quick_settings_set_wifi_name(
    quick_settings_t *settings, const char *name)
{
    if(settings == NULL) return;
    quick_settings_copy_wifi_name(settings, name);
    quick_settings_update_wifi_visual(settings);
}

static quick_settings_finger_t *quick_settings_find_finger(
    quick_settings_t *settings, uint8_t id)
{
    for(uint8_t index = 0u; index < 2u; ++index) {
        if(settings->fingers[index].active &&
           settings->fingers[index].id == id) {
            return &settings->fingers[index];
        }
    }
    return NULL;
}

static quick_settings_finger_t *quick_settings_free_finger(
    quick_settings_t *settings)
{
    for(uint8_t index = 0u; index < 2u; ++index) {
        if(!settings->fingers[index].active) return &settings->fingers[index];
    }
    return NULL;
}

static uint8_t quick_settings_active_finger_count(
    const quick_settings_t *settings)
{
    uint8_t count = 0u;

    for(uint8_t index = 0u; index < 2u; ++index) {
        if(settings->fingers[index].active) ++count;
    }
    return count;
}

static void quick_settings_begin_two_finger_gesture(
    quick_settings_t *settings)
{
    settings->gesture_tracking = true;
    settings->gesture_eligible =
        settings->fingers[0].start_y <= settings->edge_start_height &&
        settings->fingers[1].start_y <= settings->edge_start_height &&
        settings->fingers[0].path_valid && settings->fingers[1].path_valid;
    settings->gesture_claimed = settings->gesture_eligible;
    settings->gesture_opened = false;
}

static void quick_settings_evaluate_two_finger_gesture(
    quick_settings_t *settings)
{
    int32_t first_delta_y;
    int32_t second_delta_y;
    int32_t average_delta_y;

    if(!settings->gesture_tracking || !settings->gesture_eligible ||
       settings->gesture_opened || settings->is_open ||
       quick_settings_active_finger_count(settings) != 2u) {
        return;
    }
    first_delta_y =
        settings->fingers[0].y - settings->fingers[0].start_y;
    second_delta_y =
        settings->fingers[1].y - settings->fingers[1].start_y;
    if(first_delta_y < 0 || second_delta_y < 0 ||
       !settings->fingers[0].path_valid ||
       !settings->fingers[1].path_valid) {
        return;
    }
    if(first_delta_y < (int32_t)settings->open_distance * 2 / 3 ||
       second_delta_y < (int32_t)settings->open_distance * 2 / 3) {
        return;
    }
    average_delta_y = (first_delta_y + second_delta_y) / 2;
    if(average_delta_y < (int32_t)settings->open_distance) return;
    settings->gesture_opened = true;
    quick_settings_show(settings, LV_ANIM_ON);
}

bool quick_settings_gesture_feed(
    quick_settings_t *settings,
    const quick_settings_touch_t *touches,
    uint8_t touch_count)
{
    bool claimed;

    if(settings == NULL || (touch_count > 0u && touches == NULL)) {
        return false;
    }
    for(uint8_t touch_index = 0u;
        touch_index < touch_count;
        ++touch_index) {
        const quick_settings_touch_t *touch = &touches[touch_index];
        quick_settings_finger_t *finger =
            quick_settings_find_finger(settings, touch->id);

        if(touch->pressed) {
            if(finger == NULL) {
                finger = quick_settings_free_finger(settings);
                if(finger == NULL) {
                    settings->gesture_eligible = false;
                    continue;
                }
                finger->id = touch->id;
                finger->start_x = touch->x;
                finger->start_y = touch->y;
                finger->path_valid = true;
                finger->active = true;
            }
            finger->x = touch->x;
            finger->y = touch->y;
            if(finger->y - finger->start_y <
                   -QUICK_SETTINGS_GESTURE_VERTICAL_SLOP ||
               LV_ABS(finger->x - finger->start_x) >
                   finger->y - finger->start_y +
                       QUICK_SETTINGS_GESTURE_HORIZONTAL_SLOP) {
                finger->path_valid = false;
                if(settings->gesture_tracking) {
                    settings->gesture_eligible = false;
                }
            }
            if(quick_settings_active_finger_count(settings) == 2u &&
               !settings->gesture_tracking) {
                quick_settings_begin_two_finger_gesture(settings);
            }
        }
        else if(finger != NULL) {
            finger->x = touch->x;
            finger->y = touch->y;
            finger->active = false;
            if(settings->gesture_tracking &&
               quick_settings_active_finger_count(settings) < 2u) {
                settings->gesture_eligible = false;
            }
        }
    }
    quick_settings_evaluate_two_finger_gesture(settings);
    claimed = settings->gesture_claimed;
    if(quick_settings_active_finger_count(settings) == 0u) {
        quick_settings_gesture_cancel(settings);
    }
    return claimed;
}

void quick_settings_gesture_cancel(quick_settings_t *settings)
{
    if(settings == NULL) return;
    for(uint8_t index = 0u; index < 2u; ++index) {
        settings->fingers[index].active = false;
    }
    settings->gesture_tracking = false;
    settings->gesture_eligible = false;
    settings->gesture_claimed = false;
    settings->gesture_opened = false;
    settings->gesture_close_armed = false;
}

#if LV_USE_GESTURE_RECOGNITION
void quick_settings_handle_lvgl_gesture(
    quick_settings_t *settings, lv_event_t *event)
{
    lv_indev_gesture_state_t state;
    lv_indev_t *indev;
    lv_point_t center;
    float distance;

    if(settings == NULL || event == NULL || settings->is_open ||
       lv_event_get_gesture_type(event) !=
           LV_INDEV_GESTURE_TWO_FINGERS_SWIPE ||
       lv_event_get_two_fingers_swipe_dir(event) != LV_DIR_BOTTOM) {
        return;
    }
    state = lv_event_get_gesture_state(
        event, LV_INDEV_GESTURE_TWO_FINGERS_SWIPE);
    if(state != LV_INDEV_GESTURE_STATE_RECOGNIZED) return;
    distance = lv_event_get_two_fingers_swipe_distance(event);
    if(distance < (float)settings->open_distance) {
        return;
    }
    indev = lv_event_get_indev(event);
    if(indev == NULL) return;
    lv_indev_get_point(indev, &center);
    if((float)center.y - distance > (float)settings->edge_start_height) {
        return;
    }
    quick_settings_show(settings, LV_ANIM_ON);
}
#endif
