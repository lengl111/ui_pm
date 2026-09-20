#ifndef QUICK_SETTINGS_QUICK_SETTINGS_H
#define QUICK_SETTINGS_QUICK_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct quick_settings quick_settings_t;

typedef enum {
    QUICK_SETTINGS_EVENT_OPENED = 0,
    QUICK_SETTINGS_EVENT_CLOSED,
    QUICK_SETTINGS_EVENT_WIFI_CHANGED,
    QUICK_SETTINGS_EVENT_BRIGHTNESS_CHANGED,
    QUICK_SETTINGS_EVENT_VOLUME_CHANGED,
} quick_settings_event_kind_t;

typedef enum {
    QUICK_SETTINGS_CLOSE_PROGRAMMATIC = 0,
    QUICK_SETTINGS_CLOSE_BACKDROP,
    QUICK_SETTINGS_CLOSE_SWIPE,
} quick_settings_close_reason_t;

typedef struct {
    quick_settings_event_kind_t kind;
    int32_t value;
    bool enabled;
    quick_settings_close_reason_t close_reason;
} quick_settings_event_t;

typedef void (*quick_settings_event_fn)(
    void *user_ctx, const quick_settings_event_t *event);

typedef struct {
    bool wifi_enabled;
    int32_t brightness;
    int32_t volume;
    const char *wifi_name;
} quick_settings_state_t;

typedef struct {
    uint16_t panel_height;
    uint16_t edge_start_height;
    uint16_t open_distance;
    uint16_t close_distance;
    uint16_t animation_duration_ms;
    const lv_font_t *title_font;
    const lv_font_t *body_font;
    const lv_font_t *symbol_font;
    quick_settings_event_fn on_event;
    void *user_ctx;
} quick_settings_config_t;

typedef struct {
    uint8_t id;
    int32_t x;
    int32_t y;
    bool pressed;
} quick_settings_touch_t;

quick_settings_t *quick_settings_create(
    lv_obj_t *parent, const quick_settings_config_t *config);
void quick_settings_destroy(quick_settings_t *settings);

lv_obj_t *quick_settings_root(quick_settings_t *settings);
bool quick_settings_is_open(const quick_settings_t *settings);

void quick_settings_show(
    quick_settings_t *settings, lv_anim_enable_t animation);
void quick_settings_hide(
    quick_settings_t *settings,
    lv_anim_enable_t animation,
    quick_settings_close_reason_t reason);
void quick_settings_toggle(
    quick_settings_t *settings, lv_anim_enable_t animation);

void quick_settings_set_state(
    quick_settings_t *settings, const quick_settings_state_t *state);
void quick_settings_get_state(
    const quick_settings_t *settings, quick_settings_state_t *state);
void quick_settings_set_wifi(
    quick_settings_t *settings, bool enabled);
void quick_settings_set_brightness(
    quick_settings_t *settings, int32_t value);
void quick_settings_set_volume(
    quick_settings_t *settings, int32_t value);
void quick_settings_set_wifi_name(
    quick_settings_t *settings, const char *name);

bool quick_settings_gesture_feed(
    quick_settings_t *settings,
    const quick_settings_touch_t *touches,
    uint8_t touch_count);
void quick_settings_gesture_cancel(quick_settings_t *settings);

#if LV_USE_GESTURE_RECOGNITION
void quick_settings_handle_lvgl_gesture(
    quick_settings_t *settings, lv_event_t *event);
#endif

#ifdef __cplusplus
}
#endif

#endif
