#ifndef UI_RUNTIME_LVGL_COMPAT_H
#define UI_RUNTIME_LVGL_COMPAT_H

#include <lvgl.h>

#if LVGL_VERSION_MAJOR >= 9
typedef lv_display_t ui_compat_display_t;

static inline ui_compat_display_t *ui_lv_display_get_default(void)
{
    return lv_display_get_default();
}

static inline void ui_lv_display_set_default(ui_compat_display_t *display)
{
    lv_display_set_default(display);
}

static inline lv_coord_t ui_lv_display_width(ui_compat_display_t *display)
{
    return (lv_coord_t)lv_display_get_horizontal_resolution(display);
}

static inline lv_coord_t ui_lv_display_height(ui_compat_display_t *display)
{
    return (lv_coord_t)lv_display_get_vertical_resolution(display);
}

static inline void ui_lv_load_screen(lv_obj_t *screen)
{
    lv_screen_load(screen);
}

static inline lv_obj_t *ui_lv_active_screen(ui_compat_display_t *display)
{
    return lv_display_get_screen_active(display);
}

static inline uint32_t ui_lv_child_count(lv_obj_t *object)
{
    return lv_obj_get_child_count(object);
}

static inline void ui_lv_obj_delete(lv_obj_t *object)
{
    lv_obj_delete(object);
}

static inline void ui_lv_group_delete(lv_group_t *group)
{
    lv_group_delete(group);
}

static inline ui_compat_display_t *ui_lv_indev_display(lv_indev_t *indev)
{
    return lv_indev_get_display(indev);
}

static inline lv_group_t *ui_lv_indev_group(lv_indev_t *indev)
{
    return lv_indev_get_group(indev);
}

static inline void ui_lv_anim_set_duration(
    lv_anim_t *animation, uint32_t duration_ms)
{
    lv_anim_set_duration(animation, duration_ms);
}

static inline void ui_lv_anim_set_completed_cb(
    lv_anim_t *animation, lv_anim_completed_cb_t completed_cb)
{
    lv_anim_set_completed_cb(animation, completed_cb);
}

static inline bool ui_lv_anim_delete(
    void *variable, lv_anim_exec_xcb_t execute_cb)
{
    return lv_anim_delete(variable, execute_cb);
}
#else
typedef lv_disp_t ui_compat_display_t;

static inline ui_compat_display_t *ui_lv_display_get_default(void)
{
    return lv_disp_get_default();
}

static inline void ui_lv_display_set_default(ui_compat_display_t *display)
{
    lv_disp_set_default(display);
}

static inline lv_coord_t ui_lv_display_width(ui_compat_display_t *display)
{
    return lv_disp_get_hor_res(display);
}

static inline lv_coord_t ui_lv_display_height(ui_compat_display_t *display)
{
    return lv_disp_get_ver_res(display);
}

static inline void ui_lv_load_screen(lv_obj_t *screen)
{
    lv_disp_load_scr(screen);
}

static inline lv_obj_t *ui_lv_active_screen(ui_compat_display_t *display)
{
    return lv_disp_get_scr_act(display);
}

static inline uint32_t ui_lv_child_count(lv_obj_t *object)
{
    return lv_obj_get_child_cnt(object);
}

static inline void ui_lv_obj_delete(lv_obj_t *object)
{
    lv_obj_del(object);
}

static inline void ui_lv_group_delete(lv_group_t *group)
{
    lv_group_del(group);
}

static inline ui_compat_display_t *ui_lv_indev_display(lv_indev_t *indev)
{
    if(indev == NULL || indev->driver == NULL) return NULL;
    return indev->driver->disp;
}

static inline lv_group_t *ui_lv_indev_group(lv_indev_t *indev)
{
    return indev == NULL ? NULL : indev->group;
}

static inline void ui_lv_anim_set_duration(
    lv_anim_t *animation, uint32_t duration_ms)
{
    lv_anim_set_time(animation, duration_ms);
}

static inline void ui_lv_anim_set_completed_cb(
    lv_anim_t *animation, lv_anim_ready_cb_t completed_cb)
{
    lv_anim_set_ready_cb(animation, completed_cb);
}

static inline bool ui_lv_anim_delete(
    void *variable, lv_anim_exec_xcb_t execute_cb)
{
    return lv_anim_del(variable, execute_cb);
}
#endif

#if LVGL_VERSION_MAJOR >= 9 || LV_USE_USER_DATA
#define UI_LV_ANIM_HAS_USER_DATA 1

static inline void ui_lv_anim_set_user_data(
    lv_anim_t *animation, void *user_data)
{
    lv_anim_set_user_data(animation, user_data);
}

static inline void *ui_lv_anim_get_user_data(lv_anim_t *animation)
{
    return lv_anim_get_user_data(animation);
}
#else
#define UI_LV_ANIM_HAS_USER_DATA 0

static inline void ui_lv_anim_set_user_data(
    lv_anim_t *animation, void *user_data)
{
    (void)animation;
    (void)user_data;
}

static inline void *ui_lv_anim_get_user_data(lv_anim_t *animation)
{
    (void)animation;
    return NULL;
}
#endif

static inline lv_group_t *ui_lv_obj_group(lv_obj_t *object)
{
    return (lv_group_t *)lv_obj_get_group(object);
}

#endif
