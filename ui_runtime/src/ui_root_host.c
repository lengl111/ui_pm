#include "ui_runtime_internal.h"

#define UI_TRANSITION_PROGRESS_MAX 1024

static bool is_descendant_of(lv_obj_t *object, lv_obj_t *ancestor)
{
    while(object != NULL) {
        if(object == ancestor) return true;
        object = lv_obj_get_parent(object);
    }
    return false;
}

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void transition_apply(void *variable, int32_t progress)
{
    ui_host_transition_callback_t *callback =
        (ui_host_transition_callback_t *)variable;
    ui_host_transition_t *transition;
    ui_runtime_impl_t *impl;
    int32_t remaining;
    int32_t x = 0;
    int32_t y = 0;

    if(callback == NULL || !callback->active ||
       callback->transition == NULL || callback->owner == NULL) {
        return;
    }
    transition = callback->transition;
    impl = callback->owner;
    if(impl->magic != UI_RUNTIME_MAGIC ||
       impl->runtime_generation != callback->runtime_generation ||
       impl->state == UI_RUNTIME_STATE_FAULTED ||
       impl->txn.id != callback->txn_id ||
       !transition->active || transition->callback != callback ||
       transition->source_root == NULL ||
       transition->target_root == NULL) {
        return;
    }

    if(progress < 0) progress = 0;
    if(progress > UI_TRANSITION_PROGRESS_MAX) {
        progress = UI_TRANSITION_PROGRESS_MAX;
    }
    remaining = UI_TRANSITION_PROGRESS_MAX - progress;

    switch((ui_transition_kind_t)transition->kind) {
        case UI_TRANSITION_SLIDE_LEFT:
            x = (int32_t)(
                ((int64_t)impl->host.width * remaining) /
                UI_TRANSITION_PROGRESS_MAX);
            lv_obj_set_pos(transition->target_root, (lv_coord_t)x, 0);
            lv_obj_set_pos(
                transition->source_root,
                (lv_coord_t)(x - impl->host.width), 0);
            break;
        case UI_TRANSITION_SLIDE_RIGHT:
            x = (int32_t)(
                ((int64_t)impl->host.width * remaining) /
                UI_TRANSITION_PROGRESS_MAX);
            lv_obj_set_pos(transition->target_root, (lv_coord_t)-x, 0);
            lv_obj_set_pos(
                transition->source_root,
                (lv_coord_t)(impl->host.width - x), 0);
            break;
        case UI_TRANSITION_SLIDE_UP:
            y = (int32_t)(
                ((int64_t)impl->host.height * remaining) /
                UI_TRANSITION_PROGRESS_MAX);
            lv_obj_set_pos(transition->target_root, 0, (lv_coord_t)y);
            lv_obj_set_pos(
                transition->source_root, 0,
                (lv_coord_t)(y - impl->host.height));
            break;
        case UI_TRANSITION_SLIDE_DOWN:
            y = (int32_t)(
                ((int64_t)impl->host.height * remaining) /
                UI_TRANSITION_PROGRESS_MAX);
            lv_obj_set_pos(transition->target_root, 0, (lv_coord_t)-y);
            lv_obj_set_pos(
                transition->source_root, 0,
                (lv_coord_t)(impl->host.height - y));
            break;
        case UI_TRANSITION_FADE:
            if(transition->entering) {
                lv_obj_set_style_opa(
                    transition->target_root,
                    (lv_opa_t)(((uint32_t)LV_OPA_COVER *
                                (uint32_t)progress) /
                               UI_TRANSITION_PROGRESS_MAX),
                    0);
            }
            else {
                lv_obj_set_style_opa(
                    transition->source_root,
                    (lv_opa_t)(((uint32_t)LV_OPA_COVER *
                                (uint32_t)remaining) /
                               UI_TRANSITION_PROGRESS_MAX),
                    0);
            }
            break;
        case UI_TRANSITION_NONE:
        default:
            break;
    }
}

static void transition_completed(lv_anim_t *animation)
{
    ui_host_transition_callback_t *callback;
    ui_host_transition_t *transition;
    uint32_t callback_txn_id;

    if(animation == NULL) return;
    callback = (ui_host_transition_callback_t *)animation->var;
    callback_txn_id = (uint32_t)(uintptr_t)
        ui_lv_anim_get_user_data(animation);
    if(callback == NULL || !callback->active ||
       callback->transition == NULL || callback->owner == NULL) {
        return;
    }
    transition = callback->transition;
    if(!transition->active || transition->callback != callback ||
       callback->owner->magic != UI_RUNTIME_MAGIC ||
       callback->owner->state == UI_RUNTIME_STATE_FAULTED ||
       callback->owner->runtime_generation != callback->runtime_generation ||
       callback->owner->txn.id != callback->txn_id ||
       callback_txn_id != callback->txn_id) {
        return;
    }
    transition->animation_running = false;
    transition->completion_pending = true;
}

static void managed_root_deleted(lv_event_t *event)
{
    ui_runtime_impl_t *impl;
    lv_obj_t *target;
    uint16_t i;
    bool managed_match = false;

    if(lv_event_get_code(event) != LV_EVENT_DELETE) return;

    impl = (ui_runtime_impl_t *)lv_event_get_user_data(event);
    target = (lv_obj_t *)lv_event_get_target(event);
    if(impl == NULL || target == NULL) return;

#define UI_CLEAR_MANAGED_PTR(member)                  \
    do {                                              \
        if(impl->host.member == target) {             \
            impl->host.member = NULL;                 \
            managed_match = true;                     \
        }                                             \
    } while(0)
    UI_CLEAR_MANAGED_PTR(root_screen);
    UI_CLEAR_MANAGED_PTR(shell_host);
    UI_CLEAR_MANAGED_PTR(content_host);
    UI_CLEAR_MANAGED_PTR(fullscreen_host);
    UI_CLEAR_MANAGED_PTR(overlay_host);
    UI_CLEAR_MANAGED_PTR(notice_host);
    UI_CLEAR_MANAGED_PTR(modal_host);
    UI_CLEAR_MANAGED_PTR(transient_host);
    UI_CLEAR_MANAGED_PTR(input_shield);
#undef UI_CLEAR_MANAGED_PTR

    if(impl->host.status_slot.root == target) {
        impl->host.status_slot.root = NULL;
        managed_match = true;
    }
    for(i = 0; i < UI_RUNTIME_MAX_DEPTH + 1u; ++i) {
        if(impl->entries[i].view.root == target) {
            impl->entries[i].view.root = NULL;
            managed_match = true;
        }
    }
    for(i = 0; i < UI_RUNTIME_MAX_OVERLAYS + 1u; ++i) {
        if(impl->overlays[i].view.root == target) {
            impl->overlays[i].view.root = NULL;
            managed_match = true;
        }
    }

    if(!managed_match || impl->normal_delete_root == NULL ||
       !is_descendant_of(target, impl->normal_delete_root)) {
        ui_runtime_latch_fault(impl);
    }
}

static void style_managed_object(lv_obj_t *object, bool transparent)
{
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_GESTURE_BUBBLE);
    if(transparent) lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
}

static lv_obj_t *create_managed_object(
    ui_runtime_impl_t *impl, lv_obj_t *parent, bool transparent)
{
    lv_obj_t *object = lv_obj_create(parent);
    if(object == NULL) return NULL;

    if(lv_obj_add_event_cb(
           object, managed_root_deleted, LV_EVENT_DELETE, impl) == NULL) {
        ui_lv_obj_delete(object);
        return NULL;
    }
    style_managed_object(object, transparent);
    return object;
}

static void status_set_visible(
    ui_runtime_impl_t *impl, bool visible,
    ui_status_visibility_reason_t reason)
{
    if(impl->status_bar == NULL ||
       (impl->host.status_visibility_valid &&
        impl->host.status_visible == visible)) {
        return;
    }

    impl->host.status_visible = visible;
    impl->host.status_visibility_valid = true;
    if(impl->host.status_slot.root != NULL) {
        if(visible) {
            lv_obj_clear_flag(
                impl->host.status_slot.root, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_add_flag(
                impl->host.status_slot.root, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if(impl->status_bar->set_visible != NULL) {
        ui_runtime_callback_enter(impl);
        impl->status_bar->set_visible(impl->status_bar_ctx, visible, reason);
        ui_runtime_callback_leave(impl);
    }
}

static void status_apply_style(ui_runtime_impl_t *impl, uint16_t style)
{
    if(impl->status_bar == NULL ||
       (impl->host.status_style_valid &&
        impl->host.applied_status_style == style)) {
        return;
    }

    impl->host.applied_status_style = style;
    impl->host.status_style_valid = true;
    if(impl->status_bar->apply_style != NULL) {
        ui_runtime_callback_enter(impl);
        impl->status_bar->apply_style(impl->status_bar_ctx, style);
        ui_runtime_callback_leave(impl);
    }
}

static bool is_focus_indev(lv_indev_t *indev)
{
    lv_indev_type_t type = lv_indev_get_type(indev);
    return type == LV_INDEV_TYPE_KEYPAD || type == LV_INDEV_TYPE_ENCODER;
}

static bool slot_can_focus(const ui_view_slot_t *slot)
{
    if(slot->kind == UI_SLOT_PAGE) return true;
    if(slot->kind != UI_SLOT_OVERLAY || slot->owner == NULL ||
       slot->owner_index >= UI_RUNTIME_MAX_OVERLAYS + 1u) {
        return false;
    }
    return slot->owner->overlays[slot->owner_index].desc != NULL &&
           slot->owner->overlays[slot->owner_index].desc->lane ==
               UI_OVERLAY_LANE_MODAL;
}

lv_obj_t *ui_view_slot_content(ui_view_slot_t *slot)
{
    if(slot == NULL || !slot->borrowed || slot->root == NULL) return NULL;
    return slot->root;
}

ui_result_code_t ui_view_slot_focus_add(
    ui_view_slot_t *slot, lv_obj_t *object)
{
    if(slot == NULL || !slot_can_focus(slot) ||
       !slot->borrowed || slot->root == NULL ||
       object == NULL || object == slot->root ||
       !is_descendant_of(object, slot->root)) {
        return UI_ERR_INVALID_ARGUMENT;
    }

    if(slot->focus_group == NULL) {
        slot->focus_group = lv_group_create();
        if(slot->focus_group == NULL) return UI_ERR_HOST;
    }
    lv_group_add_obj(slot->focus_group, object);
    return UI_OK;
}

ui_result_code_t ui_view_slot_focus_initial(
    ui_view_slot_t *slot, lv_obj_t *object)
{
    ui_result_code_t result;

    if(slot == NULL || !slot->borrowed || slot->root == NULL ||
       object == NULL || object == slot->root ||
       !is_descendant_of(object, slot->root)) {
        return UI_ERR_INVALID_ARGUMENT;
    }
    if(slot->focus_group == NULL ||
       ui_lv_obj_group(object) != slot->focus_group) {
        result = ui_view_slot_focus_add(slot, object);
        if(result != UI_OK) return result;
    }
    lv_group_focus_obj(object);
    return UI_OK;
}

ui_result_code_t ui_root_host_init(
    ui_runtime_impl_t *impl, lv_coord_t status_bar_height)
{
    ui_compat_display_t *old_default;
    uint8_t i;

    impl->host.width = ui_lv_display_width(impl->display);
    impl->host.height = ui_lv_display_height(impl->display);
    impl->host.status_height = status_bar_height;
    if(impl->host.width <= 0 || impl->host.height <= 0) {
        return UI_ERR_INVALID_ARGUMENT;
    }

    old_default = ui_lv_display_get_default();
    ui_lv_display_set_default(impl->display);

    impl->host.root_screen = create_managed_object(impl, NULL, false);
    if(impl->host.root_screen == NULL) goto host_failure;
    lv_obj_set_size(
        impl->host.root_screen, impl->host.width, impl->host.height);

    impl->host.shell_host = create_managed_object(
        impl, impl->host.root_screen, true);
    impl->host.fullscreen_host = create_managed_object(
        impl, impl->host.root_screen, true);
    impl->host.overlay_host = create_managed_object(
        impl, impl->host.root_screen, true);
    if(impl->host.shell_host == NULL ||
       impl->host.fullscreen_host == NULL ||
       impl->host.overlay_host == NULL) goto host_failure;

    lv_obj_set_size(
        impl->host.shell_host, impl->host.width, impl->host.height);
    lv_obj_set_size(
        impl->host.fullscreen_host, impl->host.width, impl->host.height);
    lv_obj_set_size(
        impl->host.overlay_host, impl->host.width, impl->host.height);

    if(impl->status_bar != NULL) {
        ui_view_slot_t *status_slot = &impl->host.status_slot;
        memset(status_slot, 0, sizeof(*status_slot));
        status_slot->owner = impl;
        status_slot->owner_index = UI_INDEX_NONE;
        status_slot->kind = UI_SLOT_STATUS_BAR;
        status_slot->root = create_managed_object(
            impl, impl->host.shell_host, true);
        if(status_slot->root == NULL) goto host_failure;
        lv_obj_set_size(
            status_slot->root, impl->host.width, status_bar_height);
        lv_obj_set_pos(status_slot->root, 0, 0);
    }

    impl->host.content_host = create_managed_object(
        impl, impl->host.shell_host, true);
    impl->host.notice_host = create_managed_object(
        impl, impl->host.overlay_host, true);
    impl->host.modal_host = create_managed_object(
        impl, impl->host.overlay_host, true);
    impl->host.transient_host = create_managed_object(
        impl, impl->host.overlay_host, true);
    impl->host.input_shield = create_managed_object(
        impl, impl->host.overlay_host, true);
    if(impl->host.content_host == NULL ||
       impl->host.notice_host == NULL ||
       impl->host.modal_host == NULL ||
       impl->host.transient_host == NULL ||
       impl->host.input_shield == NULL) goto host_failure;

    lv_obj_set_pos(impl->host.content_host, 0, status_bar_height);
    lv_obj_set_size(
        impl->host.content_host, impl->host.width,
        (lv_coord_t)(impl->host.height - status_bar_height));
    lv_obj_set_size(
        impl->host.notice_host, impl->host.width, impl->host.height);
    lv_obj_set_size(
        impl->host.modal_host, impl->host.width, impl->host.height);
    lv_obj_set_size(
        impl->host.transient_host, impl->host.width, impl->host.height);
    lv_obj_set_size(
        impl->host.input_shield, impl->host.width, impl->host.height);

    lv_obj_add_flag(impl->host.shell_host, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(impl->host.fullscreen_host, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(impl->host.input_shield, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(impl->host.input_shield, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(impl->host.overlay_host);
    lv_obj_move_foreground(impl->host.input_shield);

    for(i = 0; i < impl->indev_count; ++i) {
        if(is_focus_indev(impl->indevs[i]) &&
           impl->host.empty_group == NULL) {
            impl->host.empty_group = lv_group_create();
            if(impl->host.empty_group == NULL) goto host_failure;
        }
    }

    ui_lv_display_set_default(old_default);
    return UI_OK;

host_failure:
    ui_lv_display_set_default(old_default);
    return UI_ERR_HOST;
}

ui_result_code_t ui_root_host_mount_status(ui_runtime_impl_t *impl)
{
    ui_result_code_t mount_result;

    if(impl->status_bar == NULL) return UI_OK;
    impl->host.status_slot.borrowed = true;
    ui_runtime_callback_enter(impl);
    mount_result = impl->status_bar->mount(
        impl->status_bar_ctx, &impl->host.status_slot);
    ui_runtime_callback_leave(impl);
    impl->host.status_slot.borrowed = false;
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    if(mount_result != UI_OK) return UI_ERR_VIEW_MOUNT;

    impl->host.status_slot.mounted = true;
    impl->host.status_mounted = true;
    return UI_OK;
}

void ui_root_host_rollback(ui_runtime_impl_t *impl)
{
    uint8_t i;

    for(i = 0; i < impl->indev_count; ++i) {
        if(is_focus_indev(impl->indevs[i])) {
            lv_indev_set_group(
                impl->indevs[i], impl->previous_groups[i]);
        }
    }

    if(impl->host.status_mounted && impl->status_bar != NULL &&
       impl->status_bar->unmount != NULL &&
       impl->state != UI_RUNTIME_STATE_FAULTED) {
        impl->host.status_slot.borrowed = true;
        ui_runtime_callback_enter(impl);
        impl->status_bar->unmount(
            impl->status_bar_ctx, &impl->host.status_slot);
        ui_runtime_callback_leave(impl);
        impl->host.status_slot.borrowed = false;
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
    }
    impl->host.status_mounted = false;

    if(impl->host.status_slot.focus_group != NULL) {
        lv_group_remove_all_objs(impl->host.status_slot.focus_group);
        ui_lv_group_delete(impl->host.status_slot.focus_group);
        impl->host.status_slot.focus_group = NULL;
    }
    if(impl->host.root_screen != NULL &&
       impl->state != UI_RUNTIME_STATE_FAULTED) {
        lv_obj_t *previous_delete_root = impl->normal_delete_root;
        impl->normal_delete_root = impl->host.root_screen;
        ui_lv_obj_delete(impl->host.root_screen);
        impl->normal_delete_root = previous_delete_root;
    }
    if(impl->host.empty_group != NULL) {
        ui_lv_group_delete(impl->host.empty_group);
        impl->host.empty_group = NULL;
    }
}

ui_result_code_t ui_root_host_load(ui_runtime_impl_t *impl)
{
    ui_compat_display_t *old_default;

    if(impl->host.root_screen == NULL) return UI_ERR_HOST;
    old_default = ui_lv_display_get_default();
    ui_lv_display_set_default(impl->display);
    ui_lv_load_screen(impl->host.root_screen);
    ui_lv_display_set_default(old_default);
    if(ui_lv_active_screen(impl->display) != impl->host.root_screen) {
        return UI_ERR_HOST;
    }
    impl->host.loaded = true;
    return UI_OK;
}

ui_result_code_t ui_root_host_create_page_slot(
    ui_runtime_impl_t *impl, ui_entry_t *entry, uint16_t entry_index)
{
    lv_obj_t *parent;
    lv_coord_t height;

    if(entry == NULL || entry->route == NULL) return UI_ERR_INVALID_ARGUMENT;
    parent = entry->route->host_kind == UI_HOST_SHELL
                 ? impl->host.content_host
                 : impl->host.fullscreen_host;
    if(parent == NULL) return UI_ERR_HOST;

    memset(&entry->view, 0, sizeof(entry->view));
    entry->view.owner = impl;
    entry->view.owner_index = entry_index;
    entry->view.kind = UI_SLOT_PAGE;
    entry->view.root = create_managed_object(impl, parent, true);
    if(entry->view.root == NULL) return UI_ERR_VIEW_MOUNT;

    height = entry->route->host_kind == UI_HOST_SHELL
                 ? (lv_coord_t)(impl->host.height - impl->host.status_height)
                 : impl->host.height;
    lv_obj_set_size(entry->view.root, impl->host.width, height);
    lv_obj_set_pos(entry->view.root, 0, 0);
    lv_obj_add_flag(entry->view.root, LV_OBJ_FLAG_HIDDEN);
    return UI_OK;
}

ui_result_code_t ui_root_host_create_overlay_slot(
    ui_runtime_impl_t *impl, ui_overlay_t *overlay,
    uint16_t overlay_index)
{
    lv_obj_t *parent;

    if(overlay == NULL || overlay->desc == NULL) {
        return UI_ERR_INVALID_ARGUMENT;
    }
    switch(overlay->desc->lane) {
        case UI_OVERLAY_LANE_NOTICE:
            parent = impl->host.notice_host;
            break;
        case UI_OVERLAY_LANE_MODAL:
            parent = impl->host.modal_host;
            break;
        case UI_OVERLAY_LANE_TRANSIENT:
            parent = impl->host.transient_host;
            break;
        default:
            return UI_ERR_INVALID_ARGUMENT;
    }
    if(parent == NULL) return UI_ERR_HOST;

    memset(&overlay->view, 0, sizeof(overlay->view));
    overlay->view.owner = impl;
    overlay->view.owner_index = overlay_index;
    overlay->view.kind = UI_SLOT_OVERLAY;
    overlay->view.root = create_managed_object(impl, parent, true);
    if(overlay->view.root == NULL) return UI_ERR_VIEW_MOUNT;
    lv_obj_set_size(
        overlay->view.root, impl->host.width, impl->host.height);
    lv_obj_set_pos(overlay->view.root, 0, 0);
    lv_obj_add_flag(overlay->view.root, LV_OBJ_FLAG_HIDDEN);
    if(overlay->desc->lane == UI_OVERLAY_LANE_MODAL) {
        lv_obj_add_flag(overlay->view.root, LV_OBJ_FLAG_CLICKABLE);
    }
    return UI_OK;
}

void ui_root_host_delete_slot(
    ui_runtime_impl_t *impl, ui_view_slot_t *slot)
{
    if(slot == NULL) return;

    slot->borrowed = false;
    slot->mounted = false;
    if(slot->focus_group != NULL) {
        lv_group_remove_all_objs(slot->focus_group);
        ui_lv_group_delete(slot->focus_group);
        slot->focus_group = NULL;
    }
    if(slot->root != NULL) {
        lv_obj_t *previous_delete_root = impl->normal_delete_root;
        impl->normal_delete_root = slot->root;
        ui_lv_obj_delete(slot->root);
        impl->normal_delete_root = previous_delete_root;
        slot->root = NULL;
    }
}

void ui_root_host_hide_page(ui_entry_t *entry)
{
    if(entry != NULL && entry->view.root != NULL) {
        lv_obj_add_flag(entry->view.root, LV_OBJ_FLAG_HIDDEN);
        entry->view_state = UI_VIEW_MOUNTED_HIDDEN;
    }
}

void ui_root_host_hide_overlay(ui_overlay_t *overlay)
{
    if(overlay != NULL && overlay->view.root != NULL) {
        lv_obj_add_flag(overlay->view.root, LV_OBJ_FLAG_HIDDEN);
    }
}

static void disable_subtree_input(lv_obj_t *object)
{
    uint32_t child_count;
    uint32_t i;

    if(object == NULL) return;
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    child_count = ui_lv_child_count(object);
    for(i = 0u; i < child_count; ++i) {
        disable_subtree_input(lv_obj_get_child(object, (int32_t)i));
    }
}

void ui_root_host_enforce_overlay_input(ui_overlay_t *overlay)
{
    if(overlay == NULL || overlay->desc == NULL ||
       overlay->view.root == NULL) {
        return;
    }
    if(overlay->desc->lane == UI_OVERLAY_LANE_TRANSIENT ||
       (overlay->desc->lane == UI_OVERLAY_LANE_NOTICE &&
        !overlay->desc->pointer_interactive)) {
        disable_subtree_input(overlay->view.root);
    }
    else if(overlay->desc->lane == UI_OVERLAY_LANE_MODAL) {
        lv_obj_add_flag(overlay->view.root, LV_OBJ_FLAG_CLICKABLE);
    }
}

void ui_root_host_show_overlay(
    ui_runtime_impl_t *impl, ui_overlay_t *overlay)
{
    if(overlay == NULL || overlay->view.root == NULL ||
       overlay->desc == NULL) {
        ui_runtime_latch_fault(impl);
        return;
    }
    lv_obj_clear_flag(overlay->view.root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay->view.root);
    lv_obj_move_foreground(impl->host.overlay_host);
    lv_obj_move_foreground(impl->host.input_shield);
}

void ui_root_host_show_page(
    ui_runtime_impl_t *impl, ui_entry_t *target)
{
    bool shell;

    if(target == NULL || target->route == NULL || target->view.root == NULL) {
        ui_runtime_latch_fault(impl);
        return;
    }

    shell = target->route->host_kind == UI_HOST_SHELL;
    if(shell) {
        lv_obj_clear_flag(impl->host.shell_host, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(impl->host.fullscreen_host, LV_OBJ_FLAG_HIDDEN);
        status_apply_style(impl, target->route->status_style);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
        status_set_visible(impl, true, UI_STATUS_VISIBILITY_ROUTE);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
    }
    else {
        lv_obj_add_flag(impl->host.shell_host, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(impl->host.fullscreen_host, LV_OBJ_FLAG_HIDDEN);
        status_set_visible(impl, false, UI_STATUS_VISIBILITY_ROUTE);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
    }
    lv_obj_set_pos(target->view.root, 0, 0);
    lv_obj_set_style_opa(target->view.root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(target->view.root, LV_OBJ_FLAG_HIDDEN);
    target->view_state = UI_VIEW_VISIBLE;
    lv_obj_move_foreground(impl->host.overlay_host);
    lv_obj_move_foreground(impl->host.input_shield);
}

static void prepare_transition_hosts(
    ui_runtime_impl_t *impl, ui_entry_t *source, ui_entry_t *target,
    bool target_foreground)
{
    bool source_shell = source->route->host_kind == UI_HOST_SHELL;
    bool target_shell = target->route->host_kind == UI_HOST_SHELL;
    lv_obj_t *foreground_host;
    lv_obj_t *foreground_root;

    if(source_shell || target_shell) {
        lv_obj_clear_flag(impl->host.shell_host, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(impl->host.shell_host, LV_OBJ_FLAG_HIDDEN);
    }
    if(!source_shell || !target_shell) {
        lv_obj_clear_flag(impl->host.fullscreen_host, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(impl->host.fullscreen_host, LV_OBJ_FLAG_HIDDEN);
    }

    if(target_shell) {
        status_apply_style(impl, target->route->status_style);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
        status_set_visible(impl, true, UI_STATUS_VISIBILITY_ROUTE);
    }
    else {
        status_set_visible(impl, false, UI_STATUS_VISIBILITY_ROUTE);
    }
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return;

    lv_obj_set_pos(source->view.root, 0, 0);
    lv_obj_set_pos(target->view.root, 0, 0);
    lv_obj_set_style_opa(source->view.root, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(target->view.root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(source->view.root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(target->view.root, LV_OBJ_FLAG_HIDDEN);
    source->view_state = UI_VIEW_VISIBLE;
    target->view_state = UI_VIEW_VISIBLE;

    foreground_host = (target_foreground ? target : source)->route->host_kind ==
                              UI_HOST_SHELL
                          ? impl->host.shell_host
                          : impl->host.fullscreen_host;
    foreground_root = target_foreground
                          ? target->view.root
                          : source->view.root;
    lv_obj_move_foreground(foreground_host);
    lv_obj_move_foreground(foreground_root);
    lv_obj_move_foreground(impl->host.overlay_host);
    lv_obj_move_foreground(impl->host.input_shield);
}

ui_result_code_t ui_root_host_start_page_transition(
    ui_runtime_impl_t *impl, ui_entry_t *source, ui_entry_t *target,
    const ui_transition_desc_t *description, bool entering,
    uint32_t txn_id, bool *start_degraded)
{
    ui_host_transition_t *transition;
    ui_host_transition_callback_t *callback;
    lv_anim_t animation;
    uint32_t total_ms;
    bool target_foreground;

    if(start_degraded != NULL) *start_degraded = false;
    if(impl == NULL || source == NULL || target == NULL ||
       description == NULL || source == target ||
       source->view.root == NULL || target->view.root == NULL ||
       txn_id == 0u || impl->host.transition.active) {
        return UI_ERR_HOST;
    }

    transition = &impl->host.transition;
    memset(transition, 0, sizeof(*transition));
    transition->owner = impl;
    transition->source_root = source->view.root;
    transition->target_root = target->view.root;
    transition->runtime_generation = impl->runtime_generation;
    transition->txn_id = txn_id;
    transition->source_index = (uint16_t)(source - impl->entries);
    transition->target_index = (uint16_t)(target - impl->entries);
    transition->kind = (uint8_t)description->kind;
    transition->entering = entering;
    transition->active = true;

    impl->host.transition_callback_index ^= 1u;
    callback = &impl->host.transition_callbacks[
        impl->host.transition_callback_index];
    memset(callback, 0, sizeof(*callback));
    callback->transition = transition;
    callback->owner = impl;
    callback->runtime_generation = impl->runtime_generation;
    callback->txn_id = txn_id;
    callback->active = true;
    transition->callback = callback;

    if(description->kind == UI_TRANSITION_NONE) {
        ui_root_host_hide_page(source);
        ui_root_host_show_page(impl, target);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
        transition->completion_pending = true;
        return UI_OK;
    }

    target_foreground = entering ||
                        description->kind != UI_TRANSITION_FADE;
    prepare_transition_hosts(
        impl, source, target, target_foreground);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    transition_apply(callback, 0);

    total_ms = (uint32_t)description->delay_ms +
               (uint32_t)description->duration_ms +
               (uint32_t)UI_RUNTIME_TRANSITION_WATCHDOG_MARGIN_MS;
    if(impl->now_ms_valid) {
        transition->deadline_ms = impl->now_ms + total_ms;
        transition->deadline_armed = true;
    }
    else {
        transition->deadline_ms = total_ms;
    }

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, callback);
    lv_anim_set_exec_cb(&animation, transition_apply);
    lv_anim_set_values(&animation, 0, UI_TRANSITION_PROGRESS_MAX);
    ui_lv_anim_set_duration(&animation, description->duration_ms);
    lv_anim_set_delay(&animation, description->delay_ms);
    lv_anim_set_early_apply(&animation, true);
    ui_lv_anim_set_user_data(
        &animation, (void *)(uintptr_t)txn_id);
    ui_lv_anim_set_completed_cb(&animation, transition_completed);
    transition->animation_running = true;
    if(lv_anim_start(&animation) == NULL) {
        transition->animation_running = false;
        transition->completion_pending = true;
        if(start_degraded != NULL) *start_degraded = true;
    }
    return UI_OK;
}

bool ui_root_host_transition_is_ready(
    const ui_runtime_impl_t *impl)
{
    return impl != NULL && impl->host.transition.active &&
           impl->host.transition.completion_pending;
}

bool ui_root_host_transition_timed_out(
    ui_runtime_impl_t *impl, uint32_t now_ms)
{
    ui_host_transition_t *transition;
    if(impl == NULL) return false;
    transition = &impl->host.transition;
    if(!transition->active || transition->completion_pending) return false;
    if(!transition->deadline_armed) {
        transition->deadline_ms = now_ms + transition->deadline_ms;
        transition->deadline_armed = true;
        return false;
    }
    return deadline_reached(now_ms, transition->deadline_ms);
}

ui_result_code_t ui_root_host_finish_page_transition(
    ui_runtime_impl_t *impl)
{
    ui_host_transition_t *transition;
    ui_entry_t *source;
    ui_entry_t *target;

    if(impl == NULL || !impl->host.transition.active) return UI_ERR_HOST;
    transition = &impl->host.transition;
    if(transition->runtime_generation != impl->runtime_generation ||
       transition->txn_id != impl->txn.id ||
       transition->source_index >= UI_RUNTIME_MAX_DEPTH + 1u ||
       transition->target_index >= UI_RUNTIME_MAX_DEPTH + 1u) {
        return UI_ERR_HOST;
    }
    source = &impl->entries[transition->source_index];
    target = &impl->entries[transition->target_index];
    if(source->view.root != transition->source_root ||
       target->view.root != transition->target_root) {
        return UI_ERR_HOST;
    }

    if(transition->callback != NULL) {
        (void)ui_lv_anim_delete(transition->callback, transition_apply);
        transition->animation_running = false;
    }
    lv_obj_set_pos(source->view.root, 0, 0);
    lv_obj_set_pos(target->view.root, 0, 0);
    lv_obj_set_style_opa(source->view.root, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(target->view.root, LV_OPA_COVER, 0);
    ui_root_host_hide_page(source);
    ui_root_host_show_page(impl, target);
    if(impl->state == UI_RUNTIME_STATE_FAULTED ||
       !ui_root_host_validate(impl)) {
        return UI_ERR_HOST;
    }
    transition->callback->active = false;
    memset(transition, 0, sizeof(*transition));
    return UI_OK;
}

void ui_root_host_block_input(ui_runtime_impl_t *impl)
{
    uint8_t i;

    if(impl->host.input_shield != NULL) {
        lv_obj_clear_flag(impl->host.input_shield, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(impl->host.input_shield);
    }
    for(i = 0; i < impl->indev_count; ++i) {
        lv_indev_wait_release(impl->indevs[i]);
        if(is_focus_indev(impl->indevs[i])) {
            lv_indev_set_group(
                impl->indevs[i], impl->host.empty_group);
        }
    }
}

void ui_root_host_restore_input(
    ui_runtime_impl_t *impl, ui_entry_t *active)
{
    lv_group_t *focus_group = impl->host.empty_group;

    if(active != NULL && active->view.focus_group != NULL) {
        focus_group = active->view.focus_group;
    }

    ui_root_host_restore_input_group(impl, focus_group);
}

void ui_root_host_restore_input_group(
    ui_runtime_impl_t *impl, lv_group_t *focus_group)
{
    uint8_t i;

    if(focus_group == NULL) focus_group = impl->host.empty_group;

    for(i = 0; i < impl->indev_count; ++i) {
        if(is_focus_indev(impl->indevs[i])) {
            lv_indev_set_group(impl->indevs[i], focus_group);
        }
    }
    if(impl->host.input_shield != NULL) {
        lv_obj_add_flag(impl->host.input_shield, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_root_host_set_status_suspended(
    ui_runtime_impl_t *impl, bool suspended)
{
    if(suspended) {
        status_set_visible(
            impl, false, UI_STATUS_VISIBILITY_DISPLAY_SUSPEND);
    }
    else if(impl->stack_depth > 0u) {
        ui_entry_t *active = &impl->entries[
            impl->stack[impl->stack_depth - 1u]];
        if(active->route->host_kind == UI_HOST_SHELL) {
            status_apply_style(impl, active->route->status_style);
            if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
            status_set_visible(
                impl, true, UI_STATUS_VISIBILITY_DISPLAY_RESUME);
        }
    }
}

bool ui_root_host_validate(const ui_runtime_impl_t *impl)
{
    uint16_t i;
    uint32_t shell_page_count = 0u;
    uint32_t fullscreen_page_count = 0u;
    uint32_t overlay_counts[3] = { 0u, 0u, 0u };

    if(impl == NULL || impl->host.root_screen == NULL ||
       lv_obj_get_parent(impl->host.root_screen) != NULL ||
       (impl->host.loaded &&
        ui_lv_active_screen(impl->display) != impl->host.root_screen) ||
       impl->host.shell_host == NULL ||
       lv_obj_get_parent(impl->host.shell_host) != impl->host.root_screen ||
       impl->host.fullscreen_host == NULL ||
       lv_obj_get_parent(impl->host.fullscreen_host) !=
           impl->host.root_screen ||
       impl->host.overlay_host == NULL ||
       lv_obj_get_parent(impl->host.overlay_host) != impl->host.root_screen ||
       impl->host.content_host == NULL ||
       lv_obj_get_parent(impl->host.content_host) != impl->host.shell_host ||
       impl->host.notice_host == NULL ||
       lv_obj_get_parent(impl->host.notice_host) != impl->host.overlay_host ||
       impl->host.modal_host == NULL ||
       lv_obj_get_parent(impl->host.modal_host) != impl->host.overlay_host ||
       impl->host.transient_host == NULL ||
       lv_obj_get_parent(impl->host.transient_host) !=
           impl->host.overlay_host ||
       impl->host.input_shield == NULL ||
       lv_obj_get_parent(impl->host.input_shield) !=
           impl->host.overlay_host) {
        return false;
    }

    if(ui_lv_child_count(impl->host.root_screen) != 3u ||
       ui_lv_child_count(impl->host.shell_host) !=
           (impl->status_bar != NULL ? 2u : 1u) ||
       ui_lv_child_count(impl->host.overlay_host) != 4u) {
        return false;
    }

    if(impl->status_bar != NULL &&
       (impl->host.status_slot.root == NULL ||
        lv_obj_get_parent(impl->host.status_slot.root) !=
            impl->host.shell_host)) {
        return false;
    }

    for(i = 0; i < UI_RUNTIME_MAX_DEPTH + 1u; ++i) {
        const ui_entry_t *entry = &impl->entries[i];
        lv_obj_t *expected_parent;

        if(entry->view.root == NULL) continue;
        if(entry->route == NULL || entry->view.owner != impl ||
           entry->view.owner_index != i ||
           entry->view.kind != UI_SLOT_PAGE) {
            return false;
        }
        expected_parent = entry->route->host_kind == UI_HOST_SHELL
                              ? impl->host.content_host
                              : impl->host.fullscreen_host;
        if(entry->route->host_kind == UI_HOST_SHELL) ++shell_page_count;
        else ++fullscreen_page_count;
        if(lv_obj_get_parent(entry->view.root) != expected_parent) {
            return false;
        }
    }
    for(i = 0; i < UI_RUNTIME_MAX_OVERLAYS + 1u; ++i) {
        const ui_overlay_t *overlay = &impl->overlays[i];
        lv_obj_t *expected_parent;

        if(overlay->view.root == NULL) continue;
        if(overlay->desc == NULL || overlay->view.owner != impl ||
           overlay->view.owner_index != i ||
           overlay->view.kind != UI_SLOT_OVERLAY) {
            return false;
        }
        switch(overlay->desc->lane) {
            case UI_OVERLAY_LANE_NOTICE:
                expected_parent = impl->host.notice_host;
                ++overlay_counts[UI_OVERLAY_LANE_NOTICE];
                break;
            case UI_OVERLAY_LANE_MODAL:
                expected_parent = impl->host.modal_host;
                ++overlay_counts[UI_OVERLAY_LANE_MODAL];
                break;
            case UI_OVERLAY_LANE_TRANSIENT:
                expected_parent = impl->host.transient_host;
                ++overlay_counts[UI_OVERLAY_LANE_TRANSIENT];
                break;
            default:
                return false;
        }
        if(lv_obj_get_parent(overlay->view.root) != expected_parent) {
            return false;
        }
    }
    if(ui_lv_child_count(impl->host.content_host) != shell_page_count ||
       ui_lv_child_count(impl->host.fullscreen_host) !=
           fullscreen_page_count ||
       ui_lv_child_count(impl->host.notice_host) !=
           overlay_counts[UI_OVERLAY_LANE_NOTICE] ||
       ui_lv_child_count(impl->host.modal_host) !=
           overlay_counts[UI_OVERLAY_LANE_MODAL] ||
       ui_lv_child_count(impl->host.transient_host) !=
           overlay_counts[UI_OVERLAY_LANE_TRANSIENT]) {
        return false;
    }
    return true;
}
