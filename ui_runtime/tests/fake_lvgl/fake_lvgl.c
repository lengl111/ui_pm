#include "lvgl.h"

#include <stdlib.h>
#include <string.h>

struct lv_event_dsc_t {
    lv_event_cb_t callback;
    lv_event_code_t filter;
    void *user_data;
};

struct lv_obj_t {
    lv_obj_t *parent;
    lv_obj_t *first_child;
    lv_obj_t *next_sibling;
    lv_obj_t *all_next;
    lv_disp_t *display;
    lv_group_t *group;
    lv_event_dsc_t event;
    lv_coord_t x;
    lv_coord_t y;
    lv_coord_t width;
    lv_coord_t height;
    uint32_t flags;
    lv_opa_t opacity;
};

typedef struct fake_anim_node {
    lv_anim_t animation;
    struct fake_anim_node *next;
} fake_anim_node_t;

struct lv_disp_t {
    lv_disp_t *next;
    lv_obj_t *active_screen;
    lv_coord_t width;
    lv_coord_t height;
};

struct lv_group_t {
    lv_group_t *next;
    lv_obj_t *focused;
};

struct lv_event_t {
    lv_event_code_t code;
    lv_obj_t *target;
    void *user_data;
};

static lv_disp_t *default_display;
static lv_disp_t *displays;
static lv_obj_t *objects;
static lv_group_t *groups;
static lv_indev_t *indevs;
static fake_anim_node_t *animations;
static lv_anim_t completed_animations[4];
static uint8_t completed_animation_cursor;
static uint8_t completed_animation_count;
static size_t object_count;
static size_t group_count;
static size_t animation_count;
static int create_fail_after = -1;
static bool fail_next_anim_start;

static void unlink_object_global(lv_obj_t *object)
{
    lv_obj_t **cursor = &objects;
    while(*cursor != NULL) {
        if(*cursor == object) {
            *cursor = object->all_next;
            return;
        }
        cursor = &(*cursor)->all_next;
    }
}

static void unlink_from_parent(lv_obj_t *object)
{
    lv_obj_t **cursor;
    if(object->parent == NULL) return;
    cursor = &object->parent->first_child;
    while(*cursor != NULL) {
        if(*cursor == object) {
            *cursor = object->next_sibling;
            return;
        }
        cursor = &(*cursor)->next_sibling;
    }
}

lv_disp_t *lv_disp_get_default(void)
{
    return default_display;
}

void lv_disp_set_default(lv_disp_t *display)
{
    default_display = display;
}

lv_coord_t lv_disp_get_hor_res(lv_disp_t *display)
{
    if(display == NULL) display = default_display;
    return display == NULL ? 0 : display->width;
}

lv_coord_t lv_disp_get_ver_res(lv_disp_t *display)
{
    if(display == NULL) display = default_display;
    return display == NULL ? 0 : display->height;
}

lv_obj_t *lv_disp_get_scr_act(lv_disp_t *display)
{
    if(display == NULL) display = default_display;
    return display == NULL ? NULL : display->active_screen;
}

void lv_disp_load_scr(lv_obj_t *screen)
{
    if(screen != NULL && screen->display != NULL) {
        screen->display->active_screen = screen;
    }
}

lv_obj_t *lv_obj_create(lv_obj_t *parent)
{
    lv_obj_t *object;

    if(create_fail_after == 0) return NULL;
    if(create_fail_after > 0) --create_fail_after;
    if(parent == NULL && default_display == NULL) return NULL;

    object = (lv_obj_t *)calloc(1u, sizeof(*object));
    if(object == NULL) return NULL;
    object->parent = parent;
    object->display = parent == NULL ? default_display : parent->display;
    object->flags = LV_OBJ_FLAG_CLICKABLE |
                    LV_OBJ_FLAG_SCROLLABLE |
                    LV_OBJ_FLAG_CLICK_FOCUSABLE |
                    LV_OBJ_FLAG_GESTURE_BUBBLE;
    object->opacity = LV_OPA_COVER;
    if(parent != NULL) {
        lv_obj_t **cursor = &parent->first_child;
        while(*cursor != NULL) cursor = &(*cursor)->next_sibling;
        *cursor = object;
    }
    object->all_next = objects;
    objects = object;
    ++object_count;
    return object;
}

void lv_obj_del(lv_obj_t *object)
{
    lv_event_t event;

    if(object == NULL) return;
    if(object->event.callback != NULL &&
       (object->event.filter == LV_EVENT_ALL ||
        object->event.filter == LV_EVENT_DELETE)) {
        event.code = LV_EVENT_DELETE;
        event.target = object;
        event.user_data = object->event.user_data;
        object->event.callback(&event);
    }
    while(object->first_child != NULL) {
        lv_obj_del(object->first_child);
    }
    if(object->group != NULL) {
        if(object->group->focused == object) {
            object->group->focused = NULL;
        }
        object->group = NULL;
    }
    if(object->display != NULL &&
       object->display->active_screen == object) {
        object->display->active_screen = NULL;
    }
    unlink_from_parent(object);
    unlink_object_global(object);
    free(object);
    --object_count;
}

lv_event_dsc_t *lv_obj_add_event_cb(
    lv_obj_t *object, lv_event_cb_t callback,
    lv_event_code_t filter, void *user_data)
{
    if(object == NULL || callback == NULL) return NULL;
    object->event.callback = callback;
    object->event.filter = filter;
    object->event.user_data = user_data;
    return &object->event;
}

lv_obj_t *lv_obj_get_parent(const lv_obj_t *object)
{
    return object == NULL ? NULL : object->parent;
}

uint32_t lv_obj_get_child_cnt(const lv_obj_t *object)
{
    uint32_t count = 0u;
    const lv_obj_t *child;
    if(object == NULL) return 0u;
    for(child = object->first_child; child != NULL;
        child = child->next_sibling) {
        ++count;
    }
    return count;
}

lv_obj_t *lv_obj_get_child(const lv_obj_t *object, int32_t index)
{
    lv_obj_t *child;
    int32_t i = 0;
    if(object == NULL || index < 0) return NULL;
    for(child = object->first_child; child != NULL;
        child = child->next_sibling, ++i) {
        if(i == index) return child;
    }
    return NULL;
}

void lv_obj_set_parent(lv_obj_t *object, lv_obj_t *parent)
{
    lv_obj_t **cursor;

    if(object == NULL || object == parent || object->parent == parent) return;
    unlink_from_parent(object);
    object->parent = parent;
    object->next_sibling = NULL;
    if(parent == NULL) return;
    object->display = parent->display;
    cursor = &parent->first_child;
    while(*cursor != NULL) cursor = &(*cursor)->next_sibling;
    *cursor = object;
}

void *lv_obj_get_group(const lv_obj_t *object)
{
    return object == NULL ? NULL : object->group;
}

void lv_obj_set_size(lv_obj_t *object, lv_coord_t width, lv_coord_t height)
{
    if(object == NULL) return;
    object->width = width;
    object->height = height;
}

void lv_obj_set_pos(lv_obj_t *object, lv_coord_t x, lv_coord_t y)
{
    if(object == NULL) return;
    object->x = x;
    object->y = y;
}

void lv_obj_add_flag(lv_obj_t *object, lv_obj_flag_t flag)
{
    if(object != NULL) object->flags |= (uint32_t)flag;
}

void lv_obj_clear_flag(lv_obj_t *object, lv_obj_flag_t flag)
{
    if(object != NULL) object->flags &= ~(uint32_t)flag;
}

bool lv_obj_has_flag(const lv_obj_t *object, lv_obj_flag_t flag)
{
    return object != NULL && (object->flags & (uint32_t)flag) != 0u;
}

void lv_obj_move_foreground(lv_obj_t *object)
{
    lv_obj_t **cursor;
    if(object == NULL || object->parent == NULL ||
       object->next_sibling == NULL) return;

    cursor = &object->parent->first_child;
    while(*cursor != NULL && *cursor != object) {
        cursor = &(*cursor)->next_sibling;
    }
    if(*cursor != object) return;
    *cursor = object->next_sibling;
    object->next_sibling = NULL;
    cursor = &object->parent->first_child;
    while(*cursor != NULL) cursor = &(*cursor)->next_sibling;
    *cursor = object;
}

void lv_obj_set_style_bg_opa(
    lv_obj_t *object, lv_opa_t opacity, int32_t selector)
{
    (void)object;
    (void)opacity;
    (void)selector;
}

void lv_obj_set_style_opa(
    lv_obj_t *object, lv_opa_t opacity, int32_t selector)
{
    (void)selector;
    if(object != NULL) object->opacity = opacity;
}

void lv_obj_set_style_border_width(
    lv_obj_t *object, lv_coord_t width, int32_t selector)
{
    (void)object;
    (void)width;
    (void)selector;
}

void lv_obj_set_style_pad_all(
    lv_obj_t *object, lv_coord_t value, int32_t selector)
{
    (void)object;
    (void)value;
    (void)selector;
}

void lv_obj_set_style_radius(
    lv_obj_t *object, lv_coord_t radius, int32_t selector)
{
    (void)object;
    (void)radius;
    (void)selector;
}

lv_event_code_t lv_event_get_code(lv_event_t *event)
{
    return event->code;
}

void *lv_event_get_user_data(lv_event_t *event)
{
    return event->user_data;
}

lv_obj_t *lv_event_get_target(lv_event_t *event)
{
    return event->target;
}

lv_group_t *lv_group_create(void)
{
    lv_group_t *group = (lv_group_t *)calloc(1u, sizeof(*group));
    if(group == NULL) return NULL;
    group->next = groups;
    groups = group;
    ++group_count;
    return group;
}

void lv_group_del(lv_group_t *group)
{
    lv_group_t **cursor = &groups;
    if(group == NULL) return;
    while(*cursor != NULL) {
        if(*cursor == group) {
            *cursor = group->next;
            free(group);
            --group_count;
            return;
        }
        cursor = &(*cursor)->next;
    }
}

void lv_group_add_obj(lv_group_t *group, lv_obj_t *object)
{
    if(object != NULL) object->group = group;
}

void lv_group_remove_all_objs(lv_group_t *group)
{
    lv_obj_t *object;
    for(object = objects; object != NULL; object = object->all_next) {
        if(object->group == group) object->group = NULL;
    }
    if(group != NULL) group->focused = NULL;
}

void lv_group_focus_obj(lv_obj_t *object)
{
    if(object != NULL && object->group != NULL) {
        object->group->focused = object;
    }
}

lv_indev_type_t lv_indev_get_type(const lv_indev_t *indev)
{
    return indev == NULL ? LV_INDEV_TYPE_NONE : (lv_indev_type_t)indev->type;
}

void lv_indev_set_group(lv_indev_t *indev, lv_group_t *group)
{
    if(indev != NULL) indev->group = group;
}

void lv_indev_wait_release(lv_indev_t *indev)
{
    if(indev != NULL) ++indev->wait_count;
}

void lv_anim_init(lv_anim_t *animation)
{
    if(animation == NULL) return;
    memset(animation, 0, sizeof(*animation));
    animation->early_apply = true;
}

void lv_anim_set_var(lv_anim_t *animation, void *variable)
{
    if(animation != NULL) animation->var = variable;
}

void lv_anim_set_exec_cb(
    lv_anim_t *animation, lv_anim_exec_xcb_t execute_cb)
{
    if(animation != NULL) animation->exec_cb = execute_cb;
}

void lv_anim_set_time(lv_anim_t *animation, uint32_t duration_ms)
{
    if(animation != NULL) animation->time = duration_ms;
}

void lv_anim_set_delay(lv_anim_t *animation, uint32_t delay_ms)
{
    if(animation != NULL) animation->delay = delay_ms;
}

void lv_anim_set_values(
    lv_anim_t *animation, int32_t start_value, int32_t end_value)
{
    if(animation == NULL) return;
    animation->start_value = start_value;
    animation->end_value = end_value;
}

void lv_anim_set_ready_cb(
    lv_anim_t *animation, lv_anim_ready_cb_t ready_cb)
{
    if(animation != NULL) animation->ready_cb = ready_cb;
}

void lv_anim_set_early_apply(lv_anim_t *animation, bool enabled)
{
    if(animation != NULL) animation->early_apply = enabled;
}

void lv_anim_set_user_data(lv_anim_t *animation, void *user_data)
{
    if(animation != NULL) animation->user_data = user_data;
}

void *lv_anim_get_user_data(lv_anim_t *animation)
{
    return animation == NULL ? NULL : animation->user_data;
}

bool lv_anim_del(void *variable, lv_anim_exec_xcb_t execute_cb)
{
    fake_anim_node_t **cursor = &animations;
    bool removed = false;

    while(*cursor != NULL) {
        fake_anim_node_t *node = *cursor;
        if(node->animation.var == variable &&
           (execute_cb == NULL || node->animation.exec_cb == execute_cb)) {
            *cursor = node->next;
            free(node);
            --animation_count;
            removed = true;
        }
        else {
            cursor = &node->next;
        }
    }
    return removed;
}

lv_anim_t *lv_anim_start(const lv_anim_t *animation)
{
    fake_anim_node_t *node;

    if(animation == NULL || animation->exec_cb == NULL) return NULL;
    if(fail_next_anim_start) {
        fail_next_anim_start = false;
        return NULL;
    }
    (void)lv_anim_del(animation->var, animation->exec_cb);
    node = (fake_anim_node_t *)calloc(1u, sizeof(*node));
    if(node == NULL) return NULL;
    node->animation = *animation;
    node->next = animations;
    animations = node;
    ++animation_count;
    if(node->animation.early_apply) {
        node->animation.exec_cb(
            node->animation.var, node->animation.start_value);
    }
    return &node->animation;
}

lv_disp_t *lv_test_display_create(lv_coord_t width, lv_coord_t height)
{
    lv_disp_t *display = (lv_disp_t *)calloc(1u, sizeof(*display));
    if(display == NULL) return NULL;
    display->width = width;
    display->height = height;
    display->next = displays;
    displays = display;
    if(default_display == NULL) default_display = display;
    return display;
}

lv_indev_t *lv_test_indev_create(lv_disp_t *display, lv_indev_type_t type)
{
    lv_indev_t *indev = (lv_indev_t *)calloc(1u, sizeof(*indev));
    if(indev == NULL) return NULL;
    indev->driver = (lv_indev_drv_t *)calloc(1u, sizeof(*indev->driver));
    if(indev->driver == NULL) {
        free(indev);
        return NULL;
    }
    indev->driver->disp = display;
    indev->type = (int)type;
    indev->next = indevs;
    indevs = indev;
    return indev;
}

lv_obj_t *lv_test_display_active_screen(lv_disp_t *display)
{
    return display == NULL ? NULL : display->active_screen;
}

lv_group_t *lv_test_indev_group(lv_indev_t *indev)
{
    return indev == NULL ? NULL : indev->group;
}

uint32_t lv_test_indev_wait_count(lv_indev_t *indev)
{
    return indev == NULL ? 0u : indev->wait_count;
}

size_t lv_test_object_count(void)
{
    return object_count;
}

size_t lv_test_group_count(void)
{
    return group_count;
}

int32_t lv_test_child_index(
    const lv_obj_t *parent, const lv_obj_t *child)
{
    const lv_obj_t *cursor;
    int32_t index = 0;
    if(parent == NULL || child == NULL) return -1;
    for(cursor = parent->first_child; cursor != NULL;
        cursor = cursor->next_sibling, ++index) {
        if(cursor == child) return index;
    }
    return -1;
}

lv_coord_t lv_test_object_x(const lv_obj_t *object)
{
    return object == NULL ? 0 : object->x;
}

lv_coord_t lv_test_object_y(const lv_obj_t *object)
{
    return object == NULL ? 0 : object->y;
}

lv_opa_t lv_test_object_opa(const lv_obj_t *object)
{
    return object == NULL ? LV_OPA_TRANSP : object->opacity;
}

void lv_test_fail_object_create_after(int successful_creates)
{
    create_fail_after = successful_creates;
}

void lv_test_fail_next_anim_start(void)
{
    fail_next_anim_start = true;
}

void lv_test_anim_set_progress(int32_t progress)
{
    fake_anim_node_t *node;
    for(node = animations; node != NULL; node = node->next) {
        node->animation.exec_cb(node->animation.var, progress);
    }
}

void lv_test_anim_complete_all(void)
{
    while(animations != NULL) {
        fake_anim_node_t *node = animations;
        animations = node->next;
        --animation_count;
        node->animation.exec_cb(
            node->animation.var, node->animation.end_value);
        completed_animations[completed_animation_cursor] = node->animation;
        completed_animation_cursor = (uint8_t)(
            (completed_animation_cursor + 1u) %
            (uint8_t)(sizeof(completed_animations) /
                      sizeof(completed_animations[0])));
        if(completed_animation_count <
           sizeof(completed_animations) /
               sizeof(completed_animations[0])) {
            ++completed_animation_count;
        }
        if(node->animation.ready_cb != NULL) {
            node->animation.ready_cb(&node->animation);
        }
        free(node);
    }
}

void lv_test_repeat_last_anim_ready(void)
{
    lv_test_repeat_anim_ready(0u);
}

void lv_test_repeat_anim_ready(uint8_t age)
{
    uint8_t capacity = (uint8_t)(
        sizeof(completed_animations) /
        sizeof(completed_animations[0]));
    uint8_t index;
    lv_anim_t *animation;

    if(age >= completed_animation_count) return;
    index = (uint8_t)(
        (completed_animation_cursor + capacity - 1u - age) % capacity);
    animation = &completed_animations[index];
    if(animation->ready_cb != NULL) animation->ready_cb(animation);
}

size_t lv_test_anim_count(void)
{
    return animation_count;
}

void lv_test_reset(void)
{
    while(animations != NULL) {
        fake_anim_node_t *next = animations->next;
        free(animations);
        animations = next;
    }
    while(objects != NULL) {
        lv_obj_t *next = objects->all_next;
        free(objects);
        objects = next;
    }
    while(groups != NULL) {
        lv_group_t *next = groups->next;
        free(groups);
        groups = next;
    }
    while(indevs != NULL) {
        lv_indev_t *next = indevs->next;
        free(indevs->driver);
        free(indevs);
        indevs = next;
    }
    while(displays != NULL) {
        lv_disp_t *next = displays->next;
        free(displays);
        displays = next;
    }
    default_display = NULL;
    object_count = 0u;
    group_count = 0u;
    animation_count = 0u;
    create_fail_after = -1;
    fail_next_anim_start = false;
    completed_animation_cursor = 0u;
    completed_animation_count = 0u;
    memset(completed_animations, 0, sizeof(completed_animations));
}
