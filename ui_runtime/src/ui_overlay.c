#include "ui_runtime_internal.h"

static bool overlay_args_valid(const ui_args_t *args)
{
    return args == NULL || args->size == 0u || args->data != NULL;
}

static const ui_overlay_desc_t *find_overlay_desc(
    const ui_runtime_impl_t *impl, uint16_t overlay_type)
{
    uint16_t i;
    for(i = 0; i < impl->overlay_type_count; ++i) {
        if(impl->overlay_descs[i].overlay_type == overlay_type) {
            return &impl->overlay_descs[i];
        }
    }
    return NULL;
}

static ui_overlay_handle_t make_overlay_handle(
    const ui_runtime_impl_t *impl, uint16_t index)
{
    ui_overlay_handle_t handle;
    memset(&handle, 0, sizeof(handle));
    handle.runtime_generation = impl->runtime_generation;
    handle.slot = index;
    handle.overlay_generation = impl->overlays[index].generation;
    return handle;
}

static bool overlay_impl_initialized(const ui_runtime_impl_t *impl)
{
    return impl != NULL && impl->magic == UI_RUNTIME_MAGIC &&
           impl->runtime_generation != 0u;
}

static const ui_args_t *overlay_normalized_args(const ui_args_t *args)
{
    static const ui_args_t empty = { NULL, 0u };
    return args == NULL ? &empty : args;
}

static uint16_t overlay_lane_index(ui_overlay_lane_t lane)
{
    return (uint16_t)lane;
}

static uint16_t overlay_find_free(const ui_runtime_impl_t *impl)
{
    uint16_t i;
    for(i = 0; i < UI_RUNTIME_MAX_OVERLAYS + 1u; ++i) {
        if(impl->overlays[i].role == UI_OVERLAY_FREE) return i;
    }
    return UI_INDEX_NONE;
}

static uint16_t overlay_count_lane(
    const ui_runtime_impl_t *impl, ui_overlay_lane_t lane)
{
    uint16_t count = 0u;
    uint16_t i;

    for(i = 0; i < UI_RUNTIME_MAX_OVERLAYS + 1u; ++i) {
        if(impl->overlays[i].role != UI_OVERLAY_FREE &&
           impl->overlays[i].desc != NULL &&
           impl->overlays[i].desc->lane == lane) {
            ++count;
        }
    }
    return count;
}

static ui_overlay_t *overlay_current(
    ui_runtime_impl_t *impl, ui_overlay_lane_t lane)
{
    uint16_t index = impl->overlay_lanes[overlay_lane_index(lane)].current;
    if(index == UI_INDEX_NONE || index >= UI_RUNTIME_MAX_OVERLAYS + 1u) {
        return NULL;
    }
    if(impl->overlays[index].role != UI_OVERLAY_LIVE ||
       (!impl->overlays[index].active &&
        !impl->overlays[index].display_suspended)) {
        return NULL;
    }
    return &impl->overlays[index];
}

static ui_overlay_t *overlay_find_key(
    ui_runtime_impl_t *impl, const ui_overlay_desc_t *desc, uint32_t key)
{
    uint16_t i;
    for(i = 0; i < UI_RUNTIME_MAX_OVERLAYS + 1u; ++i) {
        ui_overlay_t *overlay = &impl->overlays[i];
        if(overlay->role == UI_OVERLAY_LIVE && overlay->desc == desc &&
           overlay->key == key) {
            return overlay;
        }
    }
    return NULL;
}

static ui_overlay_t *overlay_choose_waiting(
    ui_runtime_impl_t *impl, ui_overlay_lane_t lane)
{
    ui_overlay_t *best = NULL;
    uint16_t i;

    for(i = 0; i < UI_RUNTIME_MAX_OVERLAYS + 1u; ++i) {
        ui_overlay_t *candidate = &impl->overlays[i];
        if(candidate->role != UI_OVERLAY_LIVE || candidate->active ||
           candidate->cleanup_pending || candidate->display_suspended ||
           candidate->desc == NULL || candidate->desc->lane != lane) {
            continue;
        }
        if(best == NULL || candidate->priority > best->priority ||
           (candidate->priority == best->priority &&
            candidate->fifo_sequence < best->fifo_sequence)) {
            best = candidate;
        }
    }
    return best;
}

static void overlay_call_destroy(
    ui_runtime_impl_t *impl, ui_overlay_t *overlay)
{
    void *state;

    if(!overlay->state_owned) return;
    state = overlay->state;
    overlay->state_owned = false;
    overlay->state = NULL;
    if(overlay->desc->adapter->destroy_state != NULL) {
        ui_runtime_callback_enter(impl);
        overlay->desc->adapter->destroy_state(
            overlay->desc->user_ctx, state);
        ui_runtime_callback_leave(impl);
    }
}

static void overlay_call_dismiss(
    ui_runtime_impl_t *impl, ui_overlay_t *overlay)
{
    if(overlay->dismiss_notified) return;
    overlay->dismiss_notified = true;
    if(overlay->desc->adapter->on_dismiss != NULL) {
        ui_runtime_callback_enter(impl);
        overlay->desc->adapter->on_dismiss(
            overlay->desc->user_ctx, overlay->state,
            overlay->dismiss_reason);
        ui_runtime_callback_leave(impl);
    }
}

static void overlay_call_deactivate(
    ui_runtime_impl_t *impl, ui_overlay_t *overlay,
    ui_overlay_deactivate_reason_t reason)
{
    if(!overlay->active) return;
    if(overlay->desc->adapter->on_deactivate != NULL) {
        overlay->view.borrowed = true;
        ui_runtime_callback_enter(impl);
        overlay->desc->adapter->on_deactivate(
            overlay->desc->user_ctx, &overlay->view,
            overlay->state, reason);
        ui_runtime_callback_leave(impl);
        overlay->view.borrowed = false;
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
    }
    overlay->active = false;
}

static void overlay_call_activate(
    ui_runtime_impl_t *impl, ui_overlay_t *overlay,
    ui_overlay_activate_reason_t reason)
{
    if(overlay->desc->adapter->on_activate != NULL) {
        overlay->view.borrowed = true;
        ui_runtime_callback_enter(impl);
        overlay->desc->adapter->on_activate(
            overlay->desc->user_ctx, &overlay->view, overlay->state, reason);
        ui_runtime_callback_leave(impl);
        overlay->view.borrowed = false;
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
    }
    overlay->active = true;
    overlay->ever_activated = true;
}

static void overlay_call_render(
    ui_runtime_impl_t *impl, ui_overlay_t *overlay)
{
    if(overlay->desc->adapter->render != NULL) {
        overlay->view.borrowed = true;
        ui_runtime_callback_enter(impl);
        overlay->desc->adapter->render(
            overlay->desc->user_ctx, &overlay->view, overlay->state);
        ui_runtime_callback_leave(impl);
        overlay->view.borrowed = false;
    }
    if(impl->state != UI_RUNTIME_STATE_FAULTED) {
        ui_root_host_enforce_overlay_input(overlay);
    }
}

static ui_result_code_t overlay_call_update(
    ui_runtime_impl_t *impl, ui_overlay_t *overlay,
    const ui_args_t *args)
{
    ui_result_code_t result;

    if(overlay->desc->adapter->update_state == NULL) {
        return UI_ERR_UNSUPPORTED;
    }
    ui_runtime_callback_enter(impl);
    result = overlay->desc->adapter->update_state(
        overlay->desc->user_ctx, overlay->state,
        overlay_normalized_args(args));
    ui_runtime_callback_leave(impl);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    return result;
}

static ui_result_code_t overlay_create_state(
    ui_runtime_impl_t *impl, ui_overlay_t *overlay,
    const ui_args_t *args)
{
    ui_result_code_t result;

    overlay->state = NULL;
    overlay->state_owned = false;
    if(overlay->desc->adapter->create_state == NULL) return UI_OK;

    ui_runtime_callback_enter(impl);
    result = overlay->desc->adapter->create_state(
        overlay->desc->user_ctx, overlay_normalized_args(args),
        &overlay->state);
    ui_runtime_callback_leave(impl);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    if(result != UI_OK) {
        overlay->state = NULL;
        return UI_ERR_STATE_CREATE;
    }
    overlay->state_owned = true;
    return UI_OK;
}

static ui_result_code_t overlay_mount(
    ui_runtime_impl_t *impl, ui_overlay_t *overlay)
{
    ui_result_code_t result;

    if(overlay->view.root == NULL) {
        result = ui_root_host_create_overlay_slot(
            impl, overlay,
            (uint16_t)(overlay - impl->overlays));
        if(result != UI_OK) return result;
    }

    overlay->view.borrowed = true;
    ui_runtime_callback_enter(impl);
    result = overlay->desc->adapter->mount_view(
        overlay->desc->user_ctx, &overlay->view, overlay->state);
    ui_runtime_callback_leave(impl);
    overlay->view.borrowed = false;
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    if(result != UI_OK) return UI_ERR_VIEW_MOUNT;
    overlay->view.mounted = true;
    ui_root_host_enforce_overlay_input(overlay);
    return UI_OK;
}

static void overlay_mark_retired(
    ui_runtime_impl_t *impl, ui_overlay_t *overlay,
    ui_overlay_dismiss_reason_t reason)
{
    ui_overlay_lane_state_t *lane;
    bool was_active;

    if(overlay == NULL || overlay->role != UI_OVERLAY_LIVE) return;
    was_active = overlay->active;
    overlay->role = UI_OVERLAY_RETIRED;
    overlay->cleanup_pending = true;
    overlay->preempt_cleanup = false;
    overlay->dismiss_reason = reason;
    lane = &impl->overlay_lanes[
        overlay_lane_index(overlay->desc->lane)];
    if(lane->current == (uint16_t)(overlay - impl->overlays)) {
        lane->current = UI_INDEX_NONE;
    }
    if(was_active) {
        ui_root_host_hide_overlay(overlay);
        overlay_call_deactivate(
            impl, overlay,
            reason == UI_OVERLAY_DISMISS_REPLACED
                ? UI_OVERLAY_DEACTIVATE_PREEMPTED
                : UI_OVERLAY_DEACTIVATE_DISMISSED);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
    }
    overlay->active = false;
}

static void overlay_mark_preempted(
    ui_runtime_impl_t *impl, ui_overlay_t *overlay)
{
    if(overlay == NULL || overlay->role != UI_OVERLAY_LIVE ||
       !overlay->active) {
        return;
    }
    ui_root_host_hide_overlay(overlay);
    overlay_call_deactivate(
        impl, overlay, UI_OVERLAY_DEACTIVATE_PREEMPTED);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
    overlay->active = false;
    overlay->cleanup_pending = true;
    overlay->preempt_cleanup = true;
}

static ui_result_code_t overlay_prepare_activate(
    ui_runtime_impl_t *impl, ui_overlay_t *overlay,
    ui_overlay_activate_reason_t reason)
{
    ui_result_code_t result;

    if(!overlay->view.mounted) {
        result = overlay_mount(impl, overlay);
        if(result != UI_OK) return result;
    }
    ui_root_host_show_overlay(impl, overlay);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    overlay_call_activate(impl, overlay, reason);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    overlay_call_render(impl, overlay);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    return UI_OK;
}

static void overlay_cleanup_one(
    ui_runtime_impl_t *impl, ui_overlay_t *overlay)
{
    ui_overlay_unmount_reason_t reason = overlay->preempt_cleanup
        ? UI_OVERLAY_UNMOUNT_PREEMPTED
        : UI_OVERLAY_UNMOUNT_DISMISSED;

    if(overlay->view.mounted) {
        overlay->view.borrowed = true;
        ui_runtime_callback_enter(impl);
        overlay->desc->adapter->unmount_view(
            overlay->desc->user_ctx, &overlay->view, overlay->state, reason);
        ui_runtime_callback_leave(impl);
        overlay->view.borrowed = false;
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
        overlay->view.mounted = false;
    }
    if(overlay->view.root != NULL) {
        ui_root_host_delete_slot(impl, &overlay->view);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
    }
    if(overlay->preempt_cleanup && overlay->role == UI_OVERLAY_LIVE) {
        memset(&overlay->view, 0, sizeof(overlay->view));
        overlay->cleanup_pending = false;
        overlay->preempt_cleanup = false;
        return;
    }
    overlay_call_dismiss(impl, overlay);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
    overlay_call_destroy(impl, overlay);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
    memset(overlay, 0, sizeof(*overlay));
}

static ui_result_code_t overlay_admission(ui_runtime_impl_t *impl)
{
    if(!overlay_impl_initialized(impl) ||
       impl->state == UI_RUNTIME_STATE_UNINITIALIZED ||
       impl->state == UI_RUNTIME_STATE_STARTING) {
        return UI_ERR_NOT_INITIALIZED;
    }
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    if(!ui_runtime_is_ui_thread(impl)) return UI_ERR_WRONG_THREAD;
    if(impl->callback_depth != 0u || impl->in_tick) {
        return UI_ERR_REENTRANT;
    }
    if(impl->state != UI_RUNTIME_STATE_RUNNING) return UI_ERR_SUSPENDED;
    return UI_OK;
}

static ui_result_code_t overlay_lane_available(
    const ui_runtime_impl_t *impl, ui_overlay_lane_t lane)
{
    uint16_t i;
    for(i = 0; i < UI_RUNTIME_MAX_OVERLAYS + 1u; ++i) {
        if(impl->overlays[i].cleanup_pending &&
           impl->overlays[i].desc != NULL &&
           impl->overlays[i].desc->lane == lane) {
            return UI_ERR_BUSY;
        }
    }
    return UI_OK;
}

static bool overlay_lane_has_cleanup(
    const ui_runtime_impl_t *impl, ui_overlay_lane_t lane)
{
    return overlay_lane_available(impl, lane) == UI_ERR_BUSY;
}

static void overlay_update_deadline(
    ui_runtime_impl_t *impl, ui_overlay_t *overlay,
    uint16_t timeout_ms)
{
    if(overlay->desc->lane != UI_OVERLAY_LANE_TRANSIENT) return;
    if(timeout_ms == 0u) timeout_ms = overlay->desc->default_transient_ms;
    overlay->deadline_ms = impl->now_ms + (uint32_t)timeout_ms;
    overlay->deadline_valid = true;
}

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static ui_result_t overlay_show_new(
    ui_runtime_impl_t *impl, const ui_overlay_desc_t *desc,
    const ui_overlay_request_t *request, ui_overlay_handle_t *out)
{
    ui_overlay_lane_state_t *lane = &impl->overlay_lanes[
        overlay_lane_index(desc->lane)];
    ui_overlay_t *overlay;
    ui_result_code_t result;
    uint16_t index;
    ui_overlay_t *current;

    if(overlay_count_lane(impl, desc->lane) >= lane->capacity) {
        if(desc->lane != UI_OVERLAY_LANE_TRANSIENT ||
           overlay_current(impl, desc->lane) == NULL) {
            return ui_result(UI_ERR_CAPACITY_FULL, 0u);
        }
    }
    index = overlay_find_free(impl);
    if(index == UI_INDEX_NONE) return ui_result(UI_ERR_CAPACITY_FULL, 0u);
    if(impl->overlay_generation_counter == UINT32_MAX ||
       impl->overlay_fifo_counter == UINT32_MAX) {
        return ui_result(UI_ERR_GENERATION_EXHAUSTED, 0u);
    }

    overlay = &impl->overlays[index];
    memset(overlay, 0, sizeof(*overlay));
    overlay->desc = desc;
    overlay->key = request->key;
    overlay->priority = request->priority;
    overlay->owner_kind = (uint8_t)request->owner_kind;
    overlay->owner = request->owner;
    overlay->generation = ++impl->overlay_generation_counter;
    overlay->fifo_sequence = ++impl->overlay_fifo_counter;
    overlay->role = UI_OVERLAY_LIVE;
    overlay_update_deadline(impl, overlay, request->timeout_ms);

    result = overlay_create_state(impl, overlay, &request->args);
    if(result != UI_OK) {
        memset(overlay, 0, sizeof(*overlay));
        return ui_result(result, 0u);
    }

    current = overlay_current(impl, desc->lane);
    if(current == NULL ||
       desc->lane == UI_OVERLAY_LANE_TRANSIENT ||
       overlay->priority > current->priority) {
        ui_root_host_block_input(impl);
        result = overlay_prepare_activate(
            impl, overlay,
            current == NULL ? UI_OVERLAY_ACTIVATE_NEW
                             : UI_OVERLAY_ACTIVATE_PREEMPTED);
        if(result != UI_OK) {
            overlay->dismiss_reason = UI_OVERLAY_DISMISS_ACTIVATION_FAILED;
            overlay->role = UI_OVERLAY_RETIRED;
            overlay->cleanup_pending = true;
            ui_overlay_restore_input(impl);
            return ui_result(result, 0u);
        }
        if(current != NULL) {
            if(desc->lane == UI_OVERLAY_LANE_TRANSIENT) {
                overlay_mark_retired(
                    impl, current, UI_OVERLAY_DISMISS_REPLACED);
            }
            else {
                overlay_mark_preempted(impl, current);
            }
            if(impl->state == UI_RUNTIME_STATE_FAULTED) {
                return ui_result(UI_ERR_FAULTED, 0u);
            }
        }
        lane->current = index;
        if(current == NULL) ui_overlay_restore_input(impl);
    }

    if(out != NULL) *out = make_overlay_handle(impl, index);
    return ui_result(UI_OK, 0u);
}

static void overlay_activate_waiting_lane(
    ui_runtime_impl_t *impl, ui_overlay_lane_t lane)
{
    uint16_t attempts = 0u;
    ui_overlay_t *candidate;
    ui_result_code_t result;
    ui_overlay_lane_state_t *lane_state = &impl->overlay_lanes[
        overlay_lane_index(lane)];

    while(lane_state->current == UI_INDEX_NONE && attempts < 2u) {
        candidate = overlay_choose_waiting(impl, lane);
        if(candidate == NULL) return;
        ++attempts;
        result = overlay_prepare_activate(
            impl, candidate,
            candidate->ever_activated
                ? UI_OVERLAY_ACTIVATE_RESUME
                : UI_OVERLAY_ACTIVATE_NEW);
        if(result != UI_OK) {
            candidate->dismiss_reason = UI_OVERLAY_DISMISS_ACTIVATION_FAILED;
            candidate->role = UI_OVERLAY_RETIRED;
            candidate->cleanup_pending = true;
            continue;
        }
        lane_state->current =
            (uint16_t)(candidate - impl->overlays);
    }
}

void ui_overlay_tick(ui_runtime_impl_t *impl, uint32_t now_ms)
{
    uint16_t i;
    ui_overlay_t *overlay;
    ui_overlay_lane_t lane;

    impl->now_ms = now_ms;
    overlay = overlay_current(impl, UI_OVERLAY_LANE_TRANSIENT);
    if(overlay != NULL && overlay->deadline_valid &&
       deadline_reached(now_ms, overlay->deadline_ms)) {
        overlay_mark_retired(
            impl, overlay, UI_OVERLAY_DISMISS_TIMEOUT);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
    }
    for(i = 0; i < UI_RUNTIME_MAX_OVERLAYS + 1u; ++i) {
        overlay = &impl->overlays[i];
        if(overlay->cleanup_pending) {
            overlay_cleanup_one(impl, overlay);
            if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
        }
    }
    for(lane = UI_OVERLAY_LANE_NOTICE;
        lane <= UI_OVERLAY_LANE_TRANSIENT; ++lane) {
        bool has_cleanup = false;
        for(i = 0; i < UI_RUNTIME_MAX_OVERLAYS + 1u; ++i) {
            if(impl->overlays[i].cleanup_pending &&
               impl->overlays[i].desc != NULL &&
               impl->overlays[i].desc->lane == lane) {
                has_cleanup = true;
                break;
            }
        }
        if(impl->state == UI_RUNTIME_STATE_RUNNING &&
           impl->overlay_lanes[overlay_lane_index(lane)].current ==
               UI_INDEX_NONE && !has_cleanup) {
            overlay_activate_waiting_lane(impl, lane);
            if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
        }
    }
    if(impl->state == UI_RUNTIME_STATE_RUNNING) {
        ui_overlay_restore_input(impl);
    }
}

void ui_overlay_suspend(ui_runtime_impl_t *impl)
{
    uint16_t i;
    for(i = 0; i < UI_RUNTIME_MAX_OVERLAYS + 1u; ++i) {
        ui_overlay_t *overlay = &impl->overlays[i];
        if(overlay->role == UI_OVERLAY_LIVE && overlay->active) {
            overlay_call_deactivate(
                impl, overlay, UI_OVERLAY_DEACTIVATE_DISPLAY_SUSPEND);
            if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
            overlay->display_suspended = true;
            ui_root_host_hide_overlay(overlay);
        }
    }
}

void ui_overlay_resume(ui_runtime_impl_t *impl)
{
    uint16_t i;
    ui_overlay_lane_t lane;
    for(i = 0; i < UI_RUNTIME_MAX_OVERLAYS + 1u; ++i) {
        ui_overlay_t *overlay = &impl->overlays[i];
        if(overlay->role == UI_OVERLAY_LIVE &&
           overlay->display_suspended) {
            ui_root_host_show_overlay(impl, overlay);
            if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
            overlay_call_activate(
                impl, overlay, UI_OVERLAY_ACTIVATE_DISPLAY_RESUME);
            if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
            overlay_call_render(impl, overlay);
            if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
            overlay->display_suspended = false;
        }
    }
    for(lane = UI_OVERLAY_LANE_NOTICE;
        lane <= UI_OVERLAY_LANE_TRANSIENT; ++lane) {
        if(impl->overlay_lanes[overlay_lane_index(lane)].current ==
               UI_INDEX_NONE &&
           !overlay_lane_has_cleanup(impl, lane)) {
            overlay_activate_waiting_lane(impl, lane);
            if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
        }
    }
}

ui_result_code_t ui_overlay_navigation_blocker(
    const ui_runtime_impl_t *impl)
{
    if(overlay_current((ui_runtime_impl_t *)impl,
                       UI_OVERLAY_LANE_MODAL) != NULL) {
        return UI_ERR_MODAL_ACTIVE;
    }
    return overlay_lane_has_cleanup(impl, UI_OVERLAY_LANE_MODAL)
               ? UI_ERR_BUSY
               : UI_OK;
}

void ui_overlay_restore_input(ui_runtime_impl_t *impl)
{
    ui_overlay_t *modal;

    if(impl->state != UI_RUNTIME_STATE_RUNNING ||
       impl->txn.phase != UI_TXN_NONE) {
        return;
    }
    modal = overlay_current(impl, UI_OVERLAY_LANE_MODAL);
    if(modal != NULL && !modal->display_suspended) {
        ui_root_host_restore_input_group(
            impl, modal->view.focus_group);
    }
    else {
        ui_root_host_restore_input(
            impl, ui_runtime_top_entry(impl));
    }
}

void ui_overlay_owner_deactivated(
    ui_runtime_impl_t *impl, ui_entry_handle_t owner)
{
    uint16_t i;

    for(i = 0; i < UI_RUNTIME_MAX_OVERLAYS + 1u; ++i) {
        ui_overlay_t *overlay = &impl->overlays[i];
        if(overlay->role == UI_OVERLAY_LIVE &&
           overlay->owner_kind == UI_OVERLAY_OWNER_ENTRY &&
           overlay->owner.runtime_generation == owner.runtime_generation &&
           overlay->owner.slot == owner.slot &&
           overlay->owner.entry_generation == owner.entry_generation) {
            overlay_mark_retired(
                impl, overlay, UI_OVERLAY_DISMISS_OWNER_DEACTIVATED);
            if(impl->state == UI_RUNTIME_STATE_FAULTED) return;
        }
    }
}

ui_result_code_t ui_overlay_back(ui_runtime_impl_t *impl)
{
    ui_overlay_t *modal = overlay_current(impl, UI_OVERLAY_LANE_MODAL);
    if(modal == NULL) {
        return overlay_lane_has_cleanup(impl, UI_OVERLAY_LANE_MODAL)
                   ? UI_OK_NO_CHANGE
                   : UI_BACK_UNHANDLED;
    }
    if(modal->desc->modal_back_policy == UI_MODAL_BACK_DISMISS) {
        overlay_mark_retired(
            impl, modal, UI_OVERLAY_DISMISS_EXPLICIT);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
        ui_root_host_block_input(impl);
    }
    return UI_OK_NO_CHANGE;
}

ui_result_t ui_overlay_show(
    ui_runtime_t *runtime, uint16_t overlay_type,
    const ui_overlay_request_t *request, ui_overlay_handle_t *out)
{
    ui_runtime_impl_t *impl;
    const ui_overlay_desc_t *desc;
    ui_overlay_t *existing;
    ui_result_code_t result;

    if(out != NULL) memset(out, 0, sizeof(*out));
    if(runtime == NULL || request == NULL ||
       !overlay_args_valid(&request->args)) {
        return ui_result(UI_ERR_INVALID_ARGUMENT, 0u);
    }
    impl = ui_runtime_impl(runtime);
    result = overlay_admission(impl);
    if(result != UI_OK) return ui_result(result, 0u);
    desc = find_overlay_desc(impl, overlay_type);
    if(desc == NULL) return ui_result(UI_ERR_INVALID_OVERLAY, 0u);
    if(request->owner_kind != UI_OVERLAY_OWNER_RUNTIME &&
       request->owner_kind != UI_OVERLAY_OWNER_ENTRY) {
        return ui_result(UI_ERR_INVALID_ARGUMENT, 0u);
    }
    if((request->owner_kind == UI_OVERLAY_OWNER_RUNTIME &&
        (request->owner.runtime_generation != 0u ||
         request->owner.slot != 0u || request->owner.reserved != 0u ||
         request->owner.entry_generation != 0u)) ||
       (desc->lane != UI_OVERLAY_LANE_TRANSIENT &&
        request->timeout_ms != 0u)) {
        return ui_result(UI_ERR_INVALID_ARGUMENT, 0u);
    }
    if(request->owner_kind == UI_OVERLAY_OWNER_ENTRY &&
       (!ui_runtime_entry_handle_valid(impl, request->owner) ||
        ui_runtime_top_entry(impl) == NULL ||
        request->owner.slot !=
            (uint16_t)(ui_runtime_top_entry(impl) - impl->entries))) {
        return ui_result(UI_ERR_INVALID_ENTRY, 0u);
    }

    existing = overlay_find_key(impl, desc, request->key);
    if(existing != NULL) {
        if(existing->priority != request->priority ||
           existing->owner_kind != (uint8_t)request->owner_kind ||
           (request->owner_kind == UI_OVERLAY_OWNER_ENTRY &&
            (existing->owner.runtime_generation !=
                 request->owner.runtime_generation ||
             existing->owner.slot != request->owner.slot ||
             existing->owner.entry_generation !=
                 request->owner.entry_generation))) {
            return ui_result(UI_ERR_INVALID_ARGUMENT, 0u);
        }
        result = overlay_call_update(
            impl, existing, &request->args);
        if(result != UI_OK) return ui_result(result, 0u);
        overlay_update_deadline(impl, existing, request->timeout_ms);
        if(existing->active) overlay_call_render(impl, existing);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) {
            return ui_result(UI_ERR_FAULTED, 0u);
        }
        if(out != NULL) *out = make_overlay_handle(
            impl, (uint16_t)(existing - impl->overlays));
        return ui_result(UI_OK, 0u);
    }

    result = overlay_lane_available(impl, desc->lane);
    if(result != UI_OK) return ui_result(result, 0u);

    return overlay_show_new(impl, desc, request, out);
}

ui_result_t ui_overlay_update(
    ui_runtime_t *runtime, ui_overlay_handle_t handle,
    const ui_args_t *args)
{
    ui_runtime_impl_t *impl;
    ui_overlay_t *overlay;
    ui_result_code_t result;

    if(runtime == NULL || !overlay_args_valid(args)) {
        return ui_result(UI_ERR_INVALID_ARGUMENT, 0u);
    }
    impl = ui_runtime_impl(runtime);
    result = overlay_admission(impl);
    if(result != UI_OK) return ui_result(result, 0u);
    if(!ui_runtime_overlay_handle_valid(impl, handle)) {
        return ui_result(UI_ERR_INVALID_OVERLAY, 0u);
    }
    overlay = &impl->overlays[handle.slot];
    result = overlay_call_update(impl, overlay, args);
    if(result != UI_OK) return ui_result(result, 0u);
    if(overlay->active) overlay_call_render(impl, overlay);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) {
        return ui_result(UI_ERR_FAULTED, 0u);
    }
    return ui_result(UI_OK, 0u);
}

ui_result_t ui_overlay_dismiss(
    ui_runtime_t *runtime, ui_overlay_handle_t handle)
{
    ui_runtime_impl_t *impl;
    ui_result_code_t result;
    bool was_active;

    if(runtime == NULL) return ui_result(UI_ERR_INVALID_ARGUMENT, 0u);
    impl = ui_runtime_impl(runtime);
    result = overlay_admission(impl);
    if(result != UI_OK) return ui_result(result, 0u);
    if(!ui_runtime_overlay_handle_valid(impl, handle)) {
        return ui_result(UI_ERR_INVALID_OVERLAY, 0u);
    }
    result = overlay_lane_available(
        impl, impl->overlays[handle.slot].desc->lane);
    if(result != UI_OK) return ui_result(result, 0u);
    was_active = impl->overlays[handle.slot].active;
    overlay_mark_retired(
        impl, &impl->overlays[handle.slot], UI_OVERLAY_DISMISS_EXPLICIT);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) {
        return ui_result(UI_ERR_FAULTED, 0u);
    }
    if(was_active) ui_root_host_block_input(impl);
    return ui_result(UI_OK, 0u);
}

bool ui_overlay_is_valid(
    const ui_runtime_t *runtime, ui_overlay_handle_t handle)
{
    const ui_runtime_impl_t *impl;
    if(runtime == NULL) return false;
    impl = ui_runtime_impl_const(runtime);
    if(!overlay_impl_initialized(impl) ||
       impl->state == UI_RUNTIME_STATE_FAULTED ||
       !ui_runtime_is_ui_thread(impl)) return false;
    return ui_runtime_overlay_handle_valid(impl, handle);
}
