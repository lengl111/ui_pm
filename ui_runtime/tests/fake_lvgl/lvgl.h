#ifndef UI_RUNTIME_TEST_FAKE_LVGL_H
#define UI_RUNTIME_TEST_FAKE_LVGL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LVGL_VERSION_MAJOR 8
#define LVGL_VERSION_MINOR 3
#define LVGL_VERSION_PATCH 10
#define LV_USE_USER_DATA 1

typedef int16_t lv_coord_t;
typedef uint8_t lv_opa_t;

#define LV_OPA_TRANSP ((lv_opa_t)0)
#define LV_OPA_COVER ((lv_opa_t)255)

typedef struct lv_obj_t lv_obj_t;
typedef struct lv_disp_t lv_disp_t;
typedef struct lv_group_t lv_group_t;
typedef struct lv_event_t lv_event_t;
typedef struct lv_event_dsc_t lv_event_dsc_t;
typedef struct _lv_anim_t lv_anim_t;

typedef void (*lv_anim_exec_xcb_t)(void *variable, int32_t value);
typedef void (*lv_anim_ready_cb_t)(lv_anim_t *animation);

struct _lv_anim_t {
    void *var;
    lv_anim_exec_xcb_t exec_cb;
    lv_anim_ready_cb_t ready_cb;
    void *user_data;
    int32_t start_value;
    int32_t end_value;
    uint32_t time;
    uint32_t delay;
    bool early_apply;
};

typedef struct lv_indev_drv_t {
    lv_disp_t *disp;
} lv_indev_drv_t;

typedef struct lv_indev_t {
    struct lv_indev_t *next;
    lv_indev_drv_t *driver;
    lv_group_t *group;
    int type;
    uint32_t wait_count;
} lv_indev_t;

typedef enum {
    LV_EVENT_ALL = 0,
    LV_EVENT_DELETE = 1,
} lv_event_code_t;

typedef enum {
    LV_INDEV_TYPE_NONE = 0,
    LV_INDEV_TYPE_POINTER,
    LV_INDEV_TYPE_KEYPAD,
    LV_INDEV_TYPE_BUTTON,
    LV_INDEV_TYPE_ENCODER,
} lv_indev_type_t;

typedef enum {
    LV_OBJ_FLAG_HIDDEN = 1u << 0,
    LV_OBJ_FLAG_CLICKABLE = 1u << 1,
    LV_OBJ_FLAG_SCROLLABLE = 1u << 2,
    LV_OBJ_FLAG_CLICK_FOCUSABLE = 1u << 3,
    LV_OBJ_FLAG_EVENT_BUBBLE = 1u << 4,
    LV_OBJ_FLAG_GESTURE_BUBBLE = 1u << 5,
} lv_obj_flag_t;

typedef void (*lv_event_cb_t)(lv_event_t *event);

lv_disp_t *lv_disp_get_default(void);
void lv_disp_set_default(lv_disp_t *display);
lv_coord_t lv_disp_get_hor_res(lv_disp_t *display);
lv_coord_t lv_disp_get_ver_res(lv_disp_t *display);
lv_obj_t *lv_disp_get_scr_act(lv_disp_t *display);
void lv_disp_load_scr(lv_obj_t *screen);

lv_obj_t *lv_obj_create(lv_obj_t *parent);
void lv_obj_del(lv_obj_t *object);
lv_event_dsc_t *lv_obj_add_event_cb(
    lv_obj_t *object, lv_event_cb_t callback,
    lv_event_code_t filter, void *user_data);
lv_obj_t *lv_obj_get_parent(const lv_obj_t *object);
uint32_t lv_obj_get_child_cnt(const lv_obj_t *object);
lv_obj_t *lv_obj_get_child(const lv_obj_t *object, int32_t index);
void lv_obj_set_parent(lv_obj_t *object, lv_obj_t *parent);
void *lv_obj_get_group(const lv_obj_t *object);
void lv_obj_set_size(lv_obj_t *object, lv_coord_t width, lv_coord_t height);
void lv_obj_set_pos(lv_obj_t *object, lv_coord_t x, lv_coord_t y);
void lv_obj_add_flag(lv_obj_t *object, lv_obj_flag_t flag);
void lv_obj_clear_flag(lv_obj_t *object, lv_obj_flag_t flag);
bool lv_obj_has_flag(const lv_obj_t *object, lv_obj_flag_t flag);
void lv_obj_move_foreground(lv_obj_t *object);
void lv_obj_set_style_bg_opa(
    lv_obj_t *object, lv_opa_t opacity, int32_t selector);
void lv_obj_set_style_opa(
    lv_obj_t *object, lv_opa_t opacity, int32_t selector);
void lv_obj_set_style_border_width(
    lv_obj_t *object, lv_coord_t width, int32_t selector);
void lv_obj_set_style_pad_all(
    lv_obj_t *object, lv_coord_t value, int32_t selector);
void lv_obj_set_style_radius(
    lv_obj_t *object, lv_coord_t radius, int32_t selector);

lv_event_code_t lv_event_get_code(lv_event_t *event);
void *lv_event_get_user_data(lv_event_t *event);
lv_obj_t *lv_event_get_target(lv_event_t *event);

lv_group_t *lv_group_create(void);
void lv_group_del(lv_group_t *group);
void lv_group_add_obj(lv_group_t *group, lv_obj_t *object);
void lv_group_remove_all_objs(lv_group_t *group);
void lv_group_focus_obj(lv_obj_t *object);

lv_indev_type_t lv_indev_get_type(const lv_indev_t *indev);
void lv_indev_set_group(lv_indev_t *indev, lv_group_t *group);
void lv_indev_wait_release(lv_indev_t *indev);

void lv_anim_init(lv_anim_t *animation);
void lv_anim_set_var(lv_anim_t *animation, void *variable);
void lv_anim_set_exec_cb(
    lv_anim_t *animation, lv_anim_exec_xcb_t execute_cb);
void lv_anim_set_time(lv_anim_t *animation, uint32_t duration_ms);
void lv_anim_set_delay(lv_anim_t *animation, uint32_t delay_ms);
void lv_anim_set_values(
    lv_anim_t *animation, int32_t start_value, int32_t end_value);
void lv_anim_set_ready_cb(
    lv_anim_t *animation, lv_anim_ready_cb_t ready_cb);
void lv_anim_set_early_apply(lv_anim_t *animation, bool enabled);
void lv_anim_set_user_data(lv_anim_t *animation, void *user_data);
void *lv_anim_get_user_data(lv_anim_t *animation);
lv_anim_t *lv_anim_start(const lv_anim_t *animation);
bool lv_anim_del(void *variable, lv_anim_exec_xcb_t execute_cb);

lv_disp_t *lv_test_display_create(lv_coord_t width, lv_coord_t height);
lv_indev_t *lv_test_indev_create(lv_disp_t *display, lv_indev_type_t type);
lv_obj_t *lv_test_display_active_screen(lv_disp_t *display);
lv_group_t *lv_test_indev_group(lv_indev_t *indev);
uint32_t lv_test_indev_wait_count(lv_indev_t *indev);
size_t lv_test_object_count(void);
size_t lv_test_group_count(void);
int32_t lv_test_child_index(
    const lv_obj_t *parent, const lv_obj_t *child);
lv_coord_t lv_test_object_x(const lv_obj_t *object);
lv_coord_t lv_test_object_y(const lv_obj_t *object);
lv_opa_t lv_test_object_opa(const lv_obj_t *object);
void lv_test_fail_object_create_after(int successful_creates);
void lv_test_fail_next_anim_start(void);
void lv_test_anim_set_progress(int32_t progress);
void lv_test_anim_complete_all(void);
void lv_test_repeat_last_anim_ready(void);
void lv_test_repeat_anim_ready(uint8_t age);
size_t lv_test_anim_count(void);
void lv_test_reset(void);

#ifdef __cplusplus
}
#endif

#endif
