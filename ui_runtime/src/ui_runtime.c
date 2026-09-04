#include "ui_runtime_internal.h"

#include <stddef.h>

_Static_assert(sizeof(ui_runtime_impl_t) <= UI_RUNTIME_STORAGE_SIZE,
               "UI_RUNTIME_STORAGE_SIZE is too small");
_Static_assert(_Alignof(ui_runtime_t) >= _Alignof(ui_runtime_impl_t),
               "ui_runtime_t alignment is insufficient");
_Static_assert(
    UI_RUNTIME_TRANSITION_WATCHDOG_MARGIN_MS <=
        (uint32_t)INT32_MAX - (uint32_t)UINT16_MAX * 2u,
    "transition watchdog interval must remain wrap-safe");

static uint32_t runtime_generation_counter;
static const ui_args_t empty_args = { NULL, 0u };

static bool args_are_valid(const ui_args_t *args)
{
    return args == NULL || args->size == 0u || args->data != NULL;
}

static const ui_args_t *normalized_args(const ui_args_t *args)
{
    return args == NULL ? &empty_args : args;
}

static void clear_handle(ui_entry_handle_t *handle)
{
    if(handle != NULL) memset(handle, 0, sizeof(*handle));
}

static bool impl_is_initialized(const ui_runtime_impl_t *impl)
{
    return impl != NULL && impl->magic == UI_RUNTIME_MAGIC &&
           impl->runtime_generation != 0u;
}

static bool is_ui_thread(const ui_runtime_impl_t *impl)
{
    return impl->is_ui_thread == NULL ||
           impl->is_ui_thread(impl->thread_ctx);
}

bool ui_runtime_is_ui_thread(const ui_runtime_impl_t *impl)
{
    return is_ui_thread(impl);
}

static const ui_route_desc_t *find_route(
    const ui_runtime_impl_t *impl, uint16_t route_id)
{
    uint16_t i;
    for(i = 0; i < impl->route_count; ++i) {
        if(impl->routes[i].route_id == route_id) return &impl->routes[i];
    }
    return NULL;
}

static ui_entry_handle_t make_handle(
    const ui_runtime_impl_t *impl, uint16_t index)
{
    ui_entry_handle_t handle;
    memset(&handle, 0, sizeof(handle));
    handle.runtime_generation = impl->runtime_generation;
    handle.slot = index;
    handle.entry_generation = impl->entries[index].generation;
    return handle;
}

static bool handle_is_valid_unchecked(
    const ui_runtime_impl_t *impl, ui_entry_handle_t handle)
{
    const ui_entry_t *entry;

    if(handle.runtime_generation == 0u ||
       handle.runtime_generation != impl->runtime_generation ||
       handle.reserved != 0u ||
       handle.slot >= UI_RUNTIME_MAX_DEPTH + 1u ||
       handle.entry_generation == 0u) {
        return false;
    }
    entry = &impl->entries[handle.slot];
    return entry->role == UI_ENTRY_LIVE &&
           entry->generation == handle.entry_generation;
}

bool ui_runtime_entry_handle_valid(
    const ui_runtime_impl_t *impl, ui_entry_handle_t handle)
{
    return impl != NULL && handle_is_valid_unchecked(impl, handle);
}

static bool overlay_handle_is_valid_unchecked(
    const ui_runtime_impl_t *impl, ui_overlay_handle_t handle)
{
    const ui_overlay_t *overlay;

    if(handle.runtime_generation == 0u ||
       handle.runtime_generation != impl->runtime_generation ||
       handle.reserved != 0u ||
       handle.slot >= UI_RUNTIME_MAX_OVERLAYS + 1u ||
       handle.overlay_generation == 0u) {
        return false;
    }
    overlay = &impl->overlays[handle.slot];
    return overlay->role == UI_OVERLAY_LIVE &&
           overlay->generation == handle.overlay_generation;
}

bool ui_runtime_overlay_handle_valid(
    const ui_runtime_impl_t *impl, ui_overlay_handle_t handle)
{
    return impl != NULL && overlay_handle_is_valid_unchecked(impl, handle);
}

static uint16_t stack_position_for_index(
    const ui_runtime_impl_t *impl, uint16_t entry_index)
{
    uint16_t i;
    for(i = 0; i < impl->stack_depth; ++i) {
        if(impl->stack[i] == entry_index) return i;
    }
    return UI_INDEX_NONE;
}

static uint16_t top_index(const ui_runtime_impl_t *impl)
{
    if(impl->stack_depth == 0u) return UI_INDEX_NONE;
    return impl->stack[impl->stack_depth - 1u];
}

ui_entry_t *ui_runtime_top_entry(ui_runtime_impl_t *impl)
{
    if(impl == NULL || impl->stack_depth == 0u) return NULL;
    return &impl->entries[top_index(impl)];
}

static void touch_entry(ui_runtime_impl_t *impl, ui_entry_t *entry)
{
    if(impl->active_sequence != UINT32_MAX) {
        ++impl->active_sequence;
    }
    entry->last_active_seq = impl->active_sequence;
}

void ui_runtime_callback_enter(ui_runtime_impl_t *impl)
{
    if(impl->callback_depth != UINT8_MAX) ++impl->callback_depth;
}

void ui_runtime_callback_leave(ui_runtime_impl_t *impl)
{
    if(impl->callback_depth > 0u) --impl->callback_depth;
    if(impl->callback_depth == 0u &&
       impl->state != UI_RUNTIME_STATE_FAULTED &&
       impl->host.root_screen != NULL &&
       !ui_root_host_validate(impl)) {
        ui_runtime_latch_fault(impl);
    }
}

void ui_runtime_latch_fault(ui_runtime_impl_t *impl)
{
    uint8_t i;

    if(!impl_is_initialized(impl) ||
       impl->state == UI_RUNTIME_STATE_FAULTED) {
        return;
    }

    impl->state = UI_RUNTIME_STATE_FAULTED;
    impl->fault_event_pending = true;
    for(i = 0; i < impl->indev_count; ++i) {
        lv_indev_wait_release(impl->indevs[i]);
        if(lv_indev_get_type(impl->indevs[i]) == LV_INDEV_TYPE_KEYPAD ||
           lv_indev_get_type(impl->indevs[i]) == LV_INDEV_TYPE_ENCODER) {
            lv_indev_set_group(impl->indevs[i], NULL);
        }
    }
}

static void publish_event(
    ui_runtime_impl_t *impl, const ui_runtime_event_t *event)
{
    if(impl->observer == NULL) return;
    ui_runtime_callback_enter(impl);
    impl->observer(impl->observer_ctx, event);
    ui_runtime_callback_leave(impl);
}

static ui_result_code_t call_create_state(
    ui_runtime_impl_t *impl, ui_entry_t *entry, const ui_args_t *args)
{
    ui_result_code_t result;

    entry->state = NULL;
    entry->state_owned = false;
    if(entry->route->adapter->create_state == NULL) return UI_OK;

    ui_runtime_callback_enter(impl);
    result = entry->route->adapter->create_state(
        entry->route->user_ctx, normalized_args(args), &entry->state);
    ui_runtime_callback_leave(impl);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    if(result != UI_OK) {
        entry->state = NULL;
        return UI_ERR_STATE_CREATE;
    }
    entry->state_owned = true;
    return UI_OK;
}

static ui_result_code_t call_mount(ui_runtime_impl_t *impl, ui_entry_t *entry)
{
    ui_result_code_t result;

    entry->view.borrowed = true;
    ui_runtime_callback_enter(impl);
    result = entry->route->adapter->mount_view(
        entry->route->user_ctx, &entry->view, entry->state);
    ui_runtime_callback_leave(impl);
    entry->view.borrowed = false;
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    if(result != UI_OK) return UI_ERR_VIEW_MOUNT;

    entry->view.mounted = true;
    entry->view_state = UI_VIEW_MOUNTED_HIDDEN;
    return UI_OK;
}

static void call_unmount(
    ui_runtime_impl_t *impl, ui_entry_t *entry,
    ui_unmount_reason_t reason)
{
    if(!entry->view.mounted) return;
    entry->view.borrowed = true;
    ui_runtime_callback_enter(impl);
    entry->route->adapter->unmount_view(
        entry->route->user_ctx, &entry->view, entry->state, reason);
    ui_runtime_callback_leave(impl);
    entry->view.borrowed = false;
    if(impl->state != UI_RUNTIME_STATE_FAULTED) {
        entry->view.mounted = false;
    }
}

static void call_destroy_state(ui_runtime_impl_t *impl, ui_entry_t *entry)
{
    void *state;

    if(!entry->state_owned) return;
    state = entry->state;
    entry->state_owned = false;
    entry->state = NULL;
    ui_runtime_callback_enter(impl);
    entry->route->adapter->destroy_state(
        entry->route->user_ctx, state);
    ui_runtime_callback_leave(impl);
}

static void call_activate(
    ui_runtime_impl_t *impl, ui_entry_t *entry,
    ui_activate_reason_t reason, const ui_args_t *reenter_args)
{
    if(entry->route->adapter->on_activate == NULL) return;
    entry->view.borrowed = true;
    ui_runtime_callback_enter(impl);
    entry->route->adapter->on_activate(
        entry->route->user_ctx, &entry->view, entry->state,
        reason, reenter_args);
    ui_runtime_callback_leave(impl);
    entry->view.borrowed = false;
}

static void call_deactivate(
    ui_runtime_impl_t *impl, ui_entry_t *entry,
    ui_deactivate_reason_t reason)
{
    if(entry->route->adapter->on_deactivate == NULL) return;
    entry->view.borrowed = true;
    ui_runtime_callback_enter(impl);
    entry->route->adapter->on_deactivate(
        entry->route->user_ctx, &entry->view, entry->state, reason);
    ui_runtime_callback_leave(impl);
    entry->view.borrowed = false;
}

static void call_render(ui_runtime_impl_t *impl, ui_entry_t *entry)
{
    if(entry->route->adapter->render == NULL) {
        entry->dirty = false;
        return;
    }
    entry->dirty = false;
    entry->view.borrowed = true;
    ui_runtime_callback_enter(impl);
    entry->route->adapter->render(
        entry->route->user_ctx, &entry->view, entry->state);
    ui_runtime_callback_leave(impl);
    entry->view.borrowed = false;
}

static ui_back_result_t call_back(
    ui_runtime_impl_t *impl, ui_entry_t *entry)
{
    ui_back_result_t result;

    if(entry->route->adapter->on_back == NULL) {
        return UI_BACK_RESULT_UNHANDLED;
    }
    entry->view.borrowed = true;
    ui_runtime_callback_enter(impl);
    result = entry->route->adapter->on_back(
        entry->route->user_ctx, &entry->view, entry->state);
    ui_runtime_callback_leave(impl);
    entry->view.borrowed = false;
    return result;
}

static ui_result_code_t validate_transition(
    const ui_transition_desc_t *transition)
{
    if(transition->kind < UI_TRANSITION_NONE ||
       transition->kind > UI_TRANSITION_FADE) {
        return UI_ERR_INVALID_ARGUMENT;
    }
    if(transition->kind == UI_TRANSITION_NONE) {
        return transition->duration_ms == 0u && transition->delay_ms == 0u
                   ? UI_OK
                   : UI_ERR_INVALID_ARGUMENT;
    }
#if !UI_LV_ANIM_HAS_USER_DATA
    return UI_ERR_UNSUPPORTED;
#endif
    if(transition->duration_ms == 0u) return UI_ERR_INVALID_ARGUMENT;
    return UI_OK;
}

static ui_result_code_t validate_config(const ui_runtime_config_t *config)
{
    uint16_t i;
    uint16_t j;
    ui_result_code_t result;
    lv_coord_t display_height;
    bool initial_route_found = false;

    if(config == NULL ||
       config->abi_version != UI_RUNTIME_ABI_VERSION ||
       config->struct_size < sizeof(*config) ||
       config->routes == NULL || config->route_count == 0u ||
       config->route_count > UI_RUNTIME_MAX_ROUTES ||
       config->overlay_type_count > UI_RUNTIME_MAX_OVERLAY_TYPES ||
       (config->overlay_type_count > 0u && config->overlays == NULL) ||
       config->modal_capacity > UI_RUNTIME_MAX_OVERLAYS ||
       config->notice_capacity > UI_RUNTIME_MAX_OVERLAYS ||
       config->transient_capacity > 1u ||
       (uint32_t)config->modal_capacity + config->notice_capacity +
               config->transient_capacity >
           UI_RUNTIME_MAX_OVERLAYS ||
       config->initial_route_id == 0u ||
       config->max_depth == 0u ||
       config->max_depth > UI_RUNTIME_MAX_DEPTH ||
       config->display == NULL ||
       config->is_ui_thread == NULL ||
       config->indev_count > UI_RUNTIME_MAX_INDEVS ||
       (config->indev_count > 0u && config->indevs == NULL) ||
       !args_are_valid(&config->initial_args)) {
        return UI_ERR_INVALID_ARGUMENT;
    }
    if(config->is_ui_thread != NULL &&
       !config->is_ui_thread(config->thread_ctx)) {
        return UI_ERR_WRONG_THREAD;
    }

    display_height = ui_lv_display_height(config->display);
    if(display_height <= 0) return UI_ERR_INVALID_ARGUMENT;
    if(config->status_bar == NULL) {
        if(config->status_bar_height != 0) return UI_ERR_INVALID_ARGUMENT;
    }
    else {
        if(config->status_bar_height <= 0 ||
           config->status_bar_height >= display_height ||
           config->status_bar->abi_version != UI_RUNTIME_ABI_VERSION ||
           config->status_bar->struct_size < sizeof(*config->status_bar) ||
           config->status_bar->mount == NULL) {
            return UI_ERR_INVALID_ARGUMENT;
        }
    }

    for(i = 0; i < config->indev_count; ++i) {
        lv_indev_type_t type;
        if(config->indevs[i] == NULL) return UI_ERR_INVALID_ARGUMENT;
        if(ui_lv_indev_display(config->indevs[i]) != config->display) {
            return UI_ERR_INVALID_ARGUMENT;
        }
        for(j = 0; j < i; ++j) {
            if(config->indevs[i] == config->indevs[j]) {
                return UI_ERR_INVALID_ARGUMENT;
            }
        }
        type = lv_indev_get_type(config->indevs[i]);
        if(type != LV_INDEV_TYPE_POINTER &&
           type != LV_INDEV_TYPE_KEYPAD &&
           type != LV_INDEV_TYPE_ENCODER &&
           type != LV_INDEV_TYPE_BUTTON) {
            return UI_ERR_INVALID_ARGUMENT;
        }
    }

    for(i = 0; i < config->route_count; ++i) {
        const ui_route_desc_t *route = &config->routes[i];
        const ui_page_adapter_t *adapter = route->adapter;

        if(route->route_id == config->initial_route_id) {
            initial_route_found = true;
        }

        if(route->abi_version != UI_RUNTIME_ABI_VERSION ||
           route->struct_size < sizeof(*route) ||
           route->route_id == 0u || adapter == NULL ||
           adapter->abi_version != UI_RUNTIME_ABI_VERSION ||
           adapter->struct_size < sizeof(*adapter) ||
           adapter->mount_view == NULL || adapter->unmount_view == NULL ||
           ((adapter->create_state == NULL) !=
            (adapter->destroy_state == NULL)) ||
           (route->host_kind != UI_HOST_SHELL &&
            route->host_kind != UI_HOST_FULLSCREEN) ||
           (route->view_policy != UI_VIEW_RETAIN &&
            route->view_policy != UI_VIEW_REBUILD) ||
           (route->launch_mode != UI_LAUNCH_STANDARD &&
            route->launch_mode != UI_LAUNCH_SINGLE_TOP &&
            route->launch_mode != UI_LAUNCH_SINGLE_TASK)) {
            return UI_ERR_INVALID_ARGUMENT;
        }
        result = validate_transition(&route->enter_transition);
        if(result != UI_OK) return result;
        result = validate_transition(&route->exit_transition);
        if(result != UI_OK) return result;
        for(j = 0; j < i; ++j) {
            if(config->routes[j].route_id == route->route_id) {
                return UI_ERR_INVALID_ARGUMENT;
            }
        }
    }
    if(!initial_route_found) {
        return UI_ERR_UNKNOWN_ROUTE;
    }

    for(i = 0; i < config->overlay_type_count; ++i) {
        const ui_overlay_desc_t *desc = &config->overlays[i];
        const ui_overlay_adapter_t *adapter = desc->adapter;

        if(desc->abi_version != UI_RUNTIME_ABI_VERSION ||
           desc->struct_size < sizeof(*desc) ||
           desc->overlay_type == 0u || adapter == NULL ||
           adapter->abi_version != UI_RUNTIME_ABI_VERSION ||
           adapter->struct_size < sizeof(*adapter) ||
           adapter->mount_view == NULL ||
           adapter->unmount_view == NULL ||
           ((adapter->create_state == NULL) !=
            (adapter->destroy_state == NULL)) ||
           desc->lane > UI_OVERLAY_LANE_TRANSIENT ||
           (desc->lane == UI_OVERLAY_LANE_MODAL &&
            !desc->pointer_interactive) ||
           (desc->lane == UI_OVERLAY_LANE_TRANSIENT &&
            desc->pointer_interactive) ||
           (desc->lane != UI_OVERLAY_LANE_MODAL &&
            desc->modal_back_policy != UI_MODAL_BACK_DISMISS) ||
           desc->modal_back_policy > UI_MODAL_BACK_CONSUME ||
           (desc->lane != UI_OVERLAY_LANE_TRANSIENT &&
            desc->default_transient_ms != 0u) ||
           (desc->lane == UI_OVERLAY_LANE_TRANSIENT &&
            desc->default_transient_ms == 0u)) {
            return UI_ERR_INVALID_ARGUMENT;
        }
        if((desc->lane == UI_OVERLAY_LANE_MODAL &&
            config->modal_capacity == 0u) ||
           (desc->lane == UI_OVERLAY_LANE_NOTICE &&
            config->notice_capacity == 0u) ||
           (desc->lane == UI_OVERLAY_LANE_TRANSIENT &&
            config->transient_capacity == 0u)) {
            return UI_ERR_INVALID_ARGUMENT;
        }
        for(j = 0; j < i; ++j) {
            if(config->overlays[j].overlay_type == desc->overlay_type) {
                return UI_ERR_INVALID_ARGUMENT;
            }
        }
    }
    return UI_OK;
}

static void copy_config(
    ui_runtime_impl_t *impl, const ui_runtime_config_t *config)
{
    uint8_t i;
    impl->routes = config->routes;
    impl->route_count = config->route_count;
    impl->max_depth = config->max_depth;
    impl->overlay_descs = config->overlays;
    impl->overlay_type_count = config->overlay_type_count;
    impl->overlay_lanes[UI_OVERLAY_LANE_NOTICE].capacity =
        config->notice_capacity;
    impl->overlay_lanes[UI_OVERLAY_LANE_MODAL].capacity =
        config->modal_capacity;
    impl->overlay_lanes[UI_OVERLAY_LANE_TRANSIENT].capacity =
        config->transient_capacity;
    impl->overlay_lanes[UI_OVERLAY_LANE_NOTICE].current = UI_INDEX_NONE;
    impl->overlay_lanes[UI_OVERLAY_LANE_MODAL].current = UI_INDEX_NONE;
    impl->overlay_lanes[UI_OVERLAY_LANE_TRANSIENT].current = UI_INDEX_NONE;
    impl->display = config->display;
    impl->indev_count = config->indev_count;
    for(i = 0; i < config->indev_count; ++i) {
        impl->indevs[i] = config->indevs[i];
        impl->previous_groups[i] = ui_lv_indev_group(config->indevs[i]);
    }
    impl->status_bar = config->status_bar;
    impl->status_bar_ctx = config->status_bar_ctx;
    impl->observer = config->observer;
    impl->observer_ctx = config->observer_ctx;
    impl->is_ui_thread = config->is_ui_thread;
    impl->thread_ctx = config->thread_ctx;
}

static uint16_t find_free_entry(const ui_runtime_impl_t *impl)
{
    uint16_t i;
    for(i = 0; i < UI_RUNTIME_MAX_DEPTH + 1u; ++i) {
        if(impl->entries[i].role == UI_ENTRY_FREE) return i;
    }
    return UI_INDEX_NONE;
}

static ui_result_code_t reserve_entry(
    ui_runtime_impl_t *impl, const ui_route_desc_t *route,
    uint16_t *out_index)
{
    uint16_t index;
    ui_entry_t *entry;

    if(impl->entry_generation_counter == UINT32_MAX) {
        return UI_ERR_GENERATION_EXHAUSTED;
    }
    index = find_free_entry(impl);
    if(index == UI_INDEX_NONE) return UI_ERR_HOST;

    entry = &impl->entries[index];
    memset(entry, 0, sizeof(*entry));
    entry->route = route;
    entry->role = UI_ENTRY_CANDIDATE;
    entry->lifecycle = UI_ENTRY_INACTIVE;
    entry->view_state = UI_VIEW_NONE;
    entry->generation = ++impl->entry_generation_counter;
    *out_index = index;
    return UI_OK;
}

static ui_result_code_t admission_result(ui_runtime_impl_t *impl)
{
    if(!impl_is_initialized(impl) ||
       impl->state == UI_RUNTIME_STATE_UNINITIALIZED ||
       impl->state == UI_RUNTIME_STATE_STARTING) {
        return UI_ERR_NOT_INITIALIZED;
    }
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    if(!is_ui_thread(impl)) return UI_ERR_WRONG_THREAD;
    if(impl->callback_depth != 0u || impl->in_tick) {
        return UI_ERR_REENTRANT;
    }
    if(impl->state == UI_RUNTIME_STATE_SUSPEND_PENDING ||
       impl->state == UI_RUNTIME_STATE_SUSPENDED ||
       impl->state == UI_RUNTIME_STATE_RESUME_PENDING) {
        return UI_ERR_SUSPENDED;
    }
    if(impl->txn.phase != UI_TXN_NONE) return UI_ERR_BUSY;
    return UI_OK;
}

static ui_result_code_t basic_mutation_result(ui_runtime_impl_t *impl)
{
    if(!impl_is_initialized(impl) ||
       impl->state == UI_RUNTIME_STATE_UNINITIALIZED ||
       impl->state == UI_RUNTIME_STATE_STARTING) {
        return UI_ERR_NOT_INITIALIZED;
    }
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    if(!is_ui_thread(impl)) return UI_ERR_WRONG_THREAD;
    if(impl->callback_depth != 0u || impl->in_tick) {
        return UI_ERR_REENTRANT;
    }
    return UI_OK;
}

static ui_result_code_t begin_transaction(
    ui_runtime_impl_t *impl, ui_nav_op_t operation,
    uint16_t source_index, uint16_t target_index,
    uint16_t to_route_id)
{
    ui_runtime_event_t started;

    if(impl->txn_counter == UINT32_MAX) {
        return UI_ERR_GENERATION_EXHAUSTED;
    }
    memset(&impl->txn, 0, sizeof(impl->txn));
    impl->txn.id = ++impl->txn_counter;
    impl->txn.phase = UI_TXN_PREPARING;
    impl->txn.operation = (uint8_t)operation;
    impl->txn.source_index = source_index;
    impl->txn.target_index = target_index;

    memset(&started, 0, sizeof(started));
    started.runtime_generation = impl->runtime_generation;
    started.txn_id = impl->txn.id;
    started.kind = UI_NAV_STARTED;
    started.code = UI_OK;
    started.from_route_id = impl->entries[source_index].route->route_id;
    started.from = make_handle(impl, source_index);
    started.from_handle_valid = true;
    started.to_route_id = to_route_id;
    if(target_index != UI_INDEX_NONE) {
        started.to = make_handle(impl, target_index);
        started.to_handle_valid = true;
    }
    impl->txn.terminal_event = started;

    ui_root_host_block_input(impl);
    publish_event(impl, &started);
    return impl->state == UI_RUNTIME_STATE_FAULTED
               ? UI_ERR_FAULTED
               : UI_OK;
}

static ui_result_t fail_transaction(
    ui_runtime_impl_t *impl, ui_result_code_t code)
{
    impl->txn.phase = UI_TXN_PREPARE_FAILED;
    impl->txn.terminal_event.kind = UI_NAV_FAILED;
    impl->txn.terminal_event.code = code;
    if(impl->txn.target_is_new) {
        memset(&impl->txn.terminal_event.to, 0,
               sizeof(impl->txn.terminal_event.to));
        impl->txn.terminal_event.to_handle_valid = false;
    }
    return ui_result(code, impl->txn.id);
}

static ui_result_code_t prepare_new_target(
    ui_runtime_impl_t *impl, const ui_route_desc_t *route,
    const ui_args_t *args)
{
    ui_result_code_t result;
    uint16_t target_index;
    ui_entry_t *target;

    result = reserve_entry(impl, route, &target_index);
    if(result != UI_OK) return result;
    impl->txn.target_index = target_index;
    impl->txn.target_is_new = true;
    target = &impl->entries[target_index];

    result = call_create_state(impl, target, args);
    if(result != UI_OK) return result;
    result = ui_root_host_create_page_slot(impl, target, target_index);
    if(result != UI_OK) return result;
    return call_mount(impl, target);
}

static ui_result_code_t prepare_existing_target(
    ui_runtime_impl_t *impl, uint16_t target_index)
{
    ui_entry_t *target = &impl->entries[target_index];
    ui_result_code_t result;

    if(target->view.mounted) {
        if(target->view.root == NULL) {
            ui_runtime_latch_fault(impl);
            return UI_ERR_FAULTED;
        }
        return UI_OK;
    }
    if(target->view.root != NULL) {
        ui_runtime_latch_fault(impl);
        return UI_ERR_FAULTED;
    }

    result = ui_root_host_create_page_slot(impl, target, target_index);
    if(result != UI_OK) return result;
    return call_mount(impl, target);
}

static void retire_entry(ui_runtime_impl_t *impl, uint16_t index)
{
    ui_entry_t *entry = &impl->entries[index];
    impl->txn.retired[impl->txn.retired_count++] = index;
    entry->role = UI_ENTRY_RETIRED;
    entry->lifecycle = UI_ENTRY_INACTIVE;
}

static void prepare_completed_event(
    ui_runtime_impl_t *impl, uint16_t source_index,
    uint16_t target_index)
{
    ui_runtime_event_t *event = &impl->txn.terminal_event;
    event->kind = UI_NAV_COMPLETED;
    event->code = UI_OK;
    event->detail_flags = UI_EVENT_DETAIL_NONE;
    event->from = make_handle(impl, source_index);
    event->from_handle_valid =
        impl->entries[source_index].role == UI_ENTRY_LIVE;
    event->to = make_handle(impl, target_index);
    event->to_handle_valid = true;
}

static void mark_transition_degraded(
    ui_runtime_impl_t *impl, uint32_t detail_flags)
{
    impl->txn.terminal_event.kind = UI_NAV_COMPLETED_DEGRADED;
    impl->txn.terminal_event.code = UI_OK;
    impl->txn.terminal_event.detail_flags |= detail_flags;
}

static ui_result_code_t commit_new_target(ui_runtime_impl_t *impl)
{
    ui_entry_t *source = &impl->entries[impl->txn.source_index];
    ui_entry_t *target = &impl->entries[impl->txn.target_index];
    bool start_degraded = false;
    ui_result_code_t result;
    uint16_t i;

    target->role = UI_ENTRY_LIVE;
    target->lifecycle = UI_ENTRY_ACTIVE;
    source->lifecycle = UI_ENTRY_INACTIVE;
    touch_entry(impl, target);

    switch((ui_nav_op_t)impl->txn.operation) {
        case UI_NAV_OP_PUSH:
            impl->stack[impl->stack_depth++] = impl->txn.target_index;
            impl->txn.source_is_covered = true;
            break;
        case UI_NAV_OP_REPLACE:
            retire_entry(impl, impl->txn.source_index);
            impl->stack[impl->stack_depth - 1u] = impl->txn.target_index;
            break;
        case UI_NAV_OP_RESET:
            for(i = impl->stack_depth; i > 0u; --i) {
                retire_entry(impl, impl->stack[i - 1u]);
            }
            impl->stack[0] = impl->txn.target_index;
            impl->stack_depth = 1u;
            break;
        default:
            ui_runtime_latch_fault(impl);
            return UI_ERR_FAULTED;
    }

    ui_overlay_owner_deactivated(
        impl, make_handle(impl, impl->txn.source_index));
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    for(i = 0; i < impl->txn.retired_count; ++i) {
        if(impl->txn.retired[i] != impl->txn.source_index) {
            ui_overlay_owner_deactivated(
                impl, make_handle(impl, impl->txn.retired[i]));
            if(impl->state == UI_RUNTIME_STATE_FAULTED) {
                return UI_ERR_FAULTED;
            }
        }
    }

    call_deactivate(
        impl, source,
        impl->txn.source_is_covered
            ? UI_DEACTIVATE_COVERED
            : UI_DEACTIVATE_REMOVED);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    call_activate(impl, target, UI_ACTIVATE_NEW, NULL);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    call_render(impl, target);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;

    prepare_completed_event(
        impl, impl->txn.source_index, impl->txn.target_index);
    result = ui_root_host_start_page_transition(
        impl, source, target, &target->route->enter_transition,
        true, impl->txn.id, &start_degraded);
    if(result != UI_OK) {
        ui_runtime_latch_fault(impl);
        return UI_ERR_FAULTED;
    }
    if(start_degraded) {
        mark_transition_degraded(impl, UI_EVENT_DETAIL_HOST_SNAP);
    }
    impl->txn.phase = UI_TXN_COMMITTED;
    return UI_OK;
}

static ui_result_code_t commit_existing_target(
    ui_runtime_impl_t *impl, uint16_t target_position,
    ui_activate_reason_t reason, const ui_args_t *reenter_args)
{
    uint16_t source_index = impl->txn.source_index;
    uint16_t target_index = impl->txn.target_index;
    ui_entry_t *source = &impl->entries[source_index];
    ui_entry_t *target = &impl->entries[target_index];
    bool start_degraded = false;
    ui_result_code_t result;
    uint16_t i;

    for(i = impl->stack_depth; i > target_position + 1u; --i) {
        retire_entry(impl, impl->stack[i - 1u]);
    }
    impl->stack_depth = (uint16_t)(target_position + 1u);
    target->lifecycle = UI_ENTRY_ACTIVE;
    touch_entry(impl, target);

    for(i = 0; i < impl->txn.retired_count; ++i) {
        ui_overlay_owner_deactivated(
            impl, make_handle(impl, impl->txn.retired[i]));
        if(impl->state == UI_RUNTIME_STATE_FAULTED) {
            return UI_ERR_FAULTED;
        }
    }

    call_deactivate(impl, source, UI_DEACTIVATE_REMOVED);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    call_activate(impl, target, reason, reenter_args);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;
    call_render(impl, target);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) return UI_ERR_FAULTED;

    prepare_completed_event(impl, source_index, target_index);
    result = ui_root_host_start_page_transition(
        impl, source, target, &source->route->exit_transition,
        false, impl->txn.id, &start_degraded);
    if(result != UI_OK) {
        ui_runtime_latch_fault(impl);
        return UI_ERR_FAULTED;
    }
    if(start_degraded) {
        mark_transition_degraded(impl, UI_EVENT_DETAIL_HOST_SNAP);
    }
    impl->txn.phase = UI_TXN_COMMITTED;
    return UI_OK;
}

static ui_result_t navigate_existing_admitted(
    ui_runtime_impl_t *impl, uint16_t target_position,
    ui_nav_op_t operation, ui_activate_reason_t reason,
    const ui_args_t *reenter_args, ui_entry_handle_t *out)
{
    uint16_t source_index = top_index(impl);
    uint16_t target_index = impl->stack[target_position];
    ui_result_code_t result;

    result = begin_transaction(
        impl, operation, source_index, target_index,
        impl->entries[target_index].route->route_id);
    if(result != UI_OK) return ui_result(result, impl->txn.id);
    result = prepare_existing_target(impl, target_index);
    if(result != UI_OK) {
        if(result == UI_ERR_FAULTED) {
            return ui_result(result, impl->txn.id);
        }
        return fail_transaction(impl, result);
    }
    result = commit_existing_target(
        impl, target_position, reason, reenter_args);
    if(result != UI_OK) return ui_result(result, impl->txn.id);

    if(out != NULL) *out = make_handle(impl, target_index);
    return ui_result(UI_OK_TRANSITIONING, impl->txn.id);
}

static ui_result_t reenter_top(
    ui_runtime_impl_t *impl, const ui_args_t *args,
    ui_entry_handle_t *out)
{
    uint16_t index = top_index(impl);
    ui_entry_t *entry = &impl->entries[index];

    call_activate(impl, entry, UI_ACTIVATE_REENTER, normalized_args(args));
    if(impl->state == UI_RUNTIME_STATE_FAULTED) {
        return ui_result(UI_ERR_FAULTED, 0u);
    }
    entry->dirty = true;
    touch_entry(impl, entry);
    if(out != NULL) *out = make_handle(impl, index);
    return ui_result(UI_OK_REENTERED, 0u);
}

static ui_result_t navigate_new(
    ui_runtime_t *runtime, uint16_t route_id, const ui_args_t *args,
    ui_entry_handle_t *out, ui_nav_op_t operation)
{
    ui_runtime_impl_t *impl;
    const ui_route_desc_t *route;
    ui_result_code_t result;
    uint16_t source_index;
    uint16_t i;

    clear_handle(out);
    if(runtime == NULL) return ui_result(UI_ERR_NOT_INITIALIZED, 0u);
    impl = ui_runtime_impl(runtime);
    result = admission_result(impl);
    if(result != UI_OK) return ui_result(result, 0u);
    result = ui_overlay_navigation_blocker(impl);
    if(result != UI_OK) return ui_result(result, 0u);
    if(!args_are_valid(args)) return ui_result(UI_ERR_INVALID_ARGUMENT, 0u);

    route = find_route(impl, route_id);
    if(route == NULL) return ui_result(UI_ERR_UNKNOWN_ROUTE, 0u);

    if(operation == UI_NAV_OP_PUSH) {
        uint16_t current_index = top_index(impl);
        if(route->launch_mode == UI_LAUNCH_SINGLE_TOP &&
           impl->entries[current_index].route == route) {
            return reenter_top(impl, args, out);
        }
        if(route->launch_mode == UI_LAUNCH_SINGLE_TASK) {
            for(i = impl->stack_depth; i > 0u; --i) {
                uint16_t index = impl->stack[i - 1u];
                if(impl->entries[index].route == route) {
                    if(i == impl->stack_depth) {
                        return reenter_top(impl, args, out);
                    }
                    return navigate_existing_admitted(
                        impl, (uint16_t)(i - 1u),
                        UI_NAV_OP_SINGLE_TASK, UI_ACTIVATE_REENTER,
                        normalized_args(args), out);
                }
            }
        }
        if(impl->stack_depth >= impl->max_depth) {
            return ui_result(UI_ERR_BACK_STACK_FULL, 0u);
        }
    }

    if(impl->entry_generation_counter == UINT32_MAX) {
        return ui_result(UI_ERR_GENERATION_EXHAUSTED, 0u);
    }
    if(find_free_entry(impl) == UI_INDEX_NONE) {
        ui_runtime_latch_fault(impl);
        return ui_result(UI_ERR_FAULTED, 0u);
    }

    source_index = top_index(impl);
    result = begin_transaction(
        impl, operation, source_index, UI_INDEX_NONE, route_id);
    if(result != UI_OK) return ui_result(result, impl->txn.id);
    result = prepare_new_target(impl, route, args);
    if(result != UI_OK) {
        if(result == UI_ERR_FAULTED) {
            return ui_result(result, impl->txn.id);
        }
        return fail_transaction(impl, result);
    }
    result = commit_new_target(impl);
    if(result != UI_OK) return ui_result(result, impl->txn.id);

    if(out != NULL) *out = make_handle(impl, impl->txn.target_index);
    return ui_result(UI_OK_TRANSITIONING, impl->txn.id);
}

static bool cleanup_failed_transaction(ui_runtime_impl_t *impl)
{
    ui_entry_t *target;

    if(impl->txn.target_index == UI_INDEX_NONE) return true;
    target = &impl->entries[impl->txn.target_index];
    if(target->view.root != NULL) {
        ui_root_host_delete_slot(impl, &target->view);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return false;
    }
    memset(&target->view, 0, sizeof(target->view));
    target->view_state = UI_VIEW_NONE;

    if(impl->txn.target_is_new) {
        call_destroy_state(impl, target);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return false;
        memset(target, 0, sizeof(*target));
    }
    return true;
}

static bool finalize_committed_transaction(ui_runtime_impl_t *impl)
{
    uint16_t i;

    if(impl->txn.source_is_covered) {
        ui_entry_t *source = &impl->entries[impl->txn.source_index];
        if(source->route->view_policy == UI_VIEW_REBUILD) {
            call_unmount(impl, source, UI_UNMOUNT_REBUILD);
            if(impl->state == UI_RUNTIME_STATE_FAULTED) return false;
            ui_root_host_delete_slot(impl, &source->view);
            if(impl->state == UI_RUNTIME_STATE_FAULTED) return false;
            source->view_state = UI_VIEW_NONE;
        }
        else {
            ui_root_host_hide_page(source);
        }
    }

    for(i = 0; i < impl->txn.retired_count; ++i) {
        ui_entry_t *entry = &impl->entries[impl->txn.retired[i]];
        if(entry->view.mounted) {
            call_unmount(impl, entry, UI_UNMOUNT_REMOVED);
            if(impl->state == UI_RUNTIME_STATE_FAULTED) return false;
        }
        if(entry->view.root != NULL) {
            ui_root_host_delete_slot(impl, &entry->view);
            if(impl->state == UI_RUNTIME_STATE_FAULTED) return false;
        }
        entry->view_state = UI_VIEW_NONE;
        call_destroy_state(impl, entry);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return false;
        memset(entry, 0, sizeof(*entry));
    }
    return true;
}

static ui_entry_t *find_oldest_hidden_view(ui_runtime_impl_t *impl)
{
    ui_entry_t *oldest = NULL;
    uint16_t active_index = top_index(impl);
    uint16_t i;

    for(i = 0; i < impl->stack_depth; ++i) {
        uint16_t index = impl->stack[i];
        ui_entry_t *entry = &impl->entries[index];
        if(index == active_index || entry->role != UI_ENTRY_LIVE ||
           !entry->view.mounted || entry->view.root == NULL) {
            continue;
        }
        if(oldest == NULL ||
           entry->last_active_seq < oldest->last_active_seq) {
            oldest = entry;
        }
    }
    return oldest;
}

static bool reclaim_pending_views(ui_runtime_impl_t *impl)
{
    bool reclaim_all = impl->pending_reclaim == 2u;
    ui_entry_t *entry;

    impl->pending_reclaim = 0u;
    do {
        entry = find_oldest_hidden_view(impl);
        if(entry == NULL) break;
        call_unmount(impl, entry, UI_UNMOUNT_RECLAIM);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return false;
        ui_root_host_delete_slot(impl, &entry->view);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return false;
        entry->view_state = UI_VIEW_NONE;
    } while(reclaim_all);
    return true;
}

static void append_runtime_event(
    ui_runtime_impl_t *impl, ui_runtime_event_t *events,
    uint8_t *event_count, ui_runtime_event_kind_t kind)
{
    ui_runtime_event_t *event = &events[(*event_count)++];
    memset(event, 0, sizeof(*event));
    event->runtime_generation = impl->runtime_generation;
    event->kind = kind;
    event->code = UI_OK;
}

static bool process_suspend_resume(
    ui_runtime_impl_t *impl, ui_runtime_event_t *events,
    uint8_t *event_count)
{
    ui_entry_t *active;

    if(impl->state == UI_RUNTIME_STATE_SUSPEND_PENDING) {
        active = &impl->entries[top_index(impl)];
        active->lifecycle = UI_ENTRY_INACTIVE;
        call_deactivate(impl, active, UI_DEACTIVATE_DISPLAY_SUSPEND);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return false;
        ui_overlay_suspend(impl);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return false;
        ui_root_host_set_status_suspended(impl, true);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return false;
        impl->state = UI_RUNTIME_STATE_SUSPENDED;
        append_runtime_event(
            impl, events, event_count, UI_RUNTIME_SUSPENDED);
    }
    else if(impl->state == UI_RUNTIME_STATE_RESUME_PENDING) {
        active = &impl->entries[top_index(impl)];
        active->lifecycle = UI_ENTRY_ACTIVE;
        ui_root_host_set_status_suspended(impl, false);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return false;
        call_activate(
            impl, active, UI_ACTIVATE_DISPLAY_RESUME, NULL);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return false;
        call_render(impl, active);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return false;
        impl->state = UI_RUNTIME_STATE_RUNNING;
        ui_overlay_resume(impl);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) return false;
        ui_overlay_restore_input(impl);
        append_runtime_event(
            impl, events, event_count, UI_RUNTIME_RESUMED);
    }
    return true;
}

static void publish_fault_if_pending(ui_runtime_impl_t *impl)
{
    ui_runtime_event_t event;

    if(!impl->fault_event_pending || impl->fault_event_delivered) return;
    impl->fault_event_pending = false;
    impl->fault_event_delivered = true;
    memset(&event, 0, sizeof(event));
    event.runtime_generation = impl->runtime_generation;
    event.kind = UI_RUNTIME_FAULTED;
    event.code = UI_ERR_HOST;
    publish_event(impl, &event);
}

ui_result_t ui_runtime_init(
    ui_runtime_t *runtime, const ui_runtime_config_t *config)
{
    ui_runtime_impl_t *impl;
    ui_result_code_t result;
    ui_entry_t *initial;
    uint16_t initial_index;

    if(runtime == NULL) return ui_result(UI_ERR_INVALID_ARGUMENT, 0u);
    impl = ui_runtime_impl(runtime);
    if(impl_is_initialized(impl) &&
       impl->state != UI_RUNTIME_STATE_UNINITIALIZED) {
        return ui_result(
            impl->state == UI_RUNTIME_STATE_FAULTED
                ? UI_ERR_FAULTED
                : UI_ERR_BUSY,
            0u);
    }

    result = validate_config(config);
    if(result != UI_OK) return ui_result(result, 0u);
    if(runtime_generation_counter == UINT32_MAX) {
        return ui_result(UI_ERR_GENERATION_EXHAUSTED, 0u);
    }

    memset(runtime, 0, sizeof(*runtime));
    impl = ui_runtime_impl(runtime);
    impl->magic = UI_RUNTIME_MAGIC;
    impl->runtime_generation = ++runtime_generation_counter;
    impl->state = UI_RUNTIME_STATE_STARTING;
    copy_config(impl, config);

    result = ui_root_host_init(impl, config->status_bar_height);
    if(result != UI_OK) goto init_failure;
    ui_root_host_block_input(impl);
    result = reserve_entry(
        impl, find_route(impl, config->initial_route_id), &initial_index);
    if(result != UI_OK) goto init_failure;
    initial = &impl->entries[initial_index];

    result = call_create_state(impl, initial, &config->initial_args);
    if(result != UI_OK) goto init_entry_failure;
    result = ui_root_host_create_page_slot(impl, initial, initial_index);
    if(result != UI_OK) goto init_entry_failure;
    result = call_mount(impl, initial);
    if(result != UI_OK) goto init_entry_failure;
    result = ui_root_host_mount_status(impl);
    if(result != UI_OK) goto init_entry_failure;
    result = ui_root_host_load(impl);
    if(result != UI_OK) goto init_entry_failure;

    initial->role = UI_ENTRY_LIVE;
    initial->lifecycle = UI_ENTRY_ACTIVE;
    impl->stack[0] = initial_index;
    impl->stack_depth = 1u;
    touch_entry(impl, initial);
    ui_root_host_show_page(impl, initial);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) {
        return ui_result(UI_ERR_FAULTED, 0u);
    }
    call_activate(impl, initial, UI_ACTIVATE_NEW, NULL);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) {
        return ui_result(UI_ERR_FAULTED, 0u);
    }
    call_render(impl, initial);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) {
        return ui_result(UI_ERR_FAULTED, 0u);
    }
    impl->state = UI_RUNTIME_STATE_RUNNING;
    ui_root_host_restore_input(impl, initial);
    return ui_result(UI_OK, 0u);

init_entry_failure:
    if(impl->state == UI_RUNTIME_STATE_FAULTED) {
        return ui_result(UI_ERR_FAULTED, 0u);
    }
    if(initial->view.mounted) {
        call_unmount(impl, initial, UI_UNMOUNT_SHUTDOWN);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) {
            return ui_result(UI_ERR_FAULTED, 0u);
        }
    }
    if(initial->view.root != NULL) {
        ui_root_host_delete_slot(impl, &initial->view);
    }
    call_destroy_state(impl, initial);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) {
        return ui_result(UI_ERR_FAULTED, 0u);
    }
init_failure:
    if(impl->state == UI_RUNTIME_STATE_FAULTED) {
        return ui_result(UI_ERR_FAULTED, 0u);
    }
    ui_root_host_rollback(impl);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) {
        return ui_result(UI_ERR_FAULTED, 0u);
    }
    memset(runtime, 0, sizeof(*runtime));
    return ui_result(result, 0u);
}

void ui_runtime_tick(ui_runtime_t *runtime, uint32_t now_ms)
{
    ui_runtime_impl_t *impl;
    ui_runtime_event_t events[3];
    uint8_t event_count = 0u;
    uint8_t i;

    if(runtime == NULL) return;
    impl = ui_runtime_impl(runtime);
    if(!impl_is_initialized(impl) || !is_ui_thread(impl) ||
       impl->callback_depth != 0u || impl->in_tick) {
        return;
    }
    if(impl->state == UI_RUNTIME_STATE_FAULTED) {
        publish_fault_if_pending(impl);
        return;
    }
    if(!ui_root_host_validate(impl)) {
        ui_runtime_latch_fault(impl);
        publish_fault_if_pending(impl);
        return;
    }
    impl->in_tick = true;
    impl->now_ms = now_ms;
    impl->now_ms_valid = true;

    if(impl->txn.phase == UI_TXN_PREPARE_FAILED) {
        ui_runtime_event_t terminal = impl->txn.terminal_event;
        if(!cleanup_failed_transaction(impl)) goto faulted;
        memset(&impl->txn, 0, sizeof(impl->txn));
        if(impl->state == UI_RUNTIME_STATE_RUNNING) {
            ui_overlay_restore_input(impl);
        }
        events[event_count++] = terminal;
    }
    else if(impl->txn.phase == UI_TXN_COMMITTED) {
        bool finish_transition = false;

        if(ui_root_host_transition_is_ready(impl)) {
            finish_transition = true;
        }
        else if(ui_root_host_transition_timed_out(impl, now_ms)) {
            mark_transition_degraded(
                impl, UI_EVENT_DETAIL_TRANSITION_TIMEOUT |
                          UI_EVENT_DETAIL_HOST_SNAP);
            finish_transition = true;
        }
        else if(impl->state == UI_RUNTIME_STATE_SUSPEND_PENDING) {
            finish_transition = true;
        }

        if(finish_transition) {
            ui_runtime_event_t terminal = impl->txn.terminal_event;
            if(ui_root_host_finish_page_transition(impl) != UI_OK) {
                ui_runtime_latch_fault(impl);
                goto faulted;
            }
            if(!finalize_committed_transaction(impl)) goto faulted;
            memset(&impl->txn, 0, sizeof(impl->txn));
            if(impl->state == UI_RUNTIME_STATE_RUNNING) {
                ui_overlay_restore_input(impl);
            }
            events[event_count++] = terminal;
        }
    }

    ui_overlay_tick(impl, now_ms);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) goto faulted;

    if(impl->pending_reclaim != 0u &&
       !reclaim_pending_views(impl)) {
        goto faulted;
    }

    if(impl->state == UI_RUNTIME_STATE_RUNNING &&
       impl->txn.phase == UI_TXN_NONE) {
        ui_entry_t *active = &impl->entries[top_index(impl)];
        if(active->dirty) {
            call_render(impl, active);
            if(impl->state == UI_RUNTIME_STATE_FAULTED) goto faulted;
        }
    }

    if(impl->txn.phase == UI_TXN_NONE &&
       !process_suspend_resume(impl, events, &event_count)) {
        goto faulted;
    }

    for(i = 0; i < event_count; ++i) {
        publish_event(impl, &events[i]);
        if(impl->state == UI_RUNTIME_STATE_FAULTED) break;
    }
    impl->in_tick = false;
    return;

faulted:
    impl->in_tick = false;
    publish_fault_if_pending(impl);
}

uint32_t ui_runtime_generation(const ui_runtime_t *runtime)
{
    const ui_runtime_impl_t *impl;
    if(runtime == NULL) return 0u;
    impl = ui_runtime_impl_const(runtime);
    return impl_is_initialized(impl) ? impl->runtime_generation : 0u;
}

ui_runtime_state_t ui_runtime_get_state(const ui_runtime_t *runtime)
{
    const ui_runtime_impl_t *impl;
    if(runtime == NULL) return UI_RUNTIME_STATE_UNINITIALIZED;
    impl = ui_runtime_impl_const(runtime);
    return impl_is_initialized(impl)
               ? impl->state
               : UI_RUNTIME_STATE_UNINITIALIZED;
}

ui_result_t ui_runtime_suspend(ui_runtime_t *runtime)
{
    ui_runtime_impl_t *impl;
    ui_result_code_t result;

    if(runtime == NULL) return ui_result(UI_ERR_NOT_INITIALIZED, 0u);
    impl = ui_runtime_impl(runtime);
    result = basic_mutation_result(impl);
    if(result != UI_OK) return ui_result(result, 0u);

    switch(impl->state) {
        case UI_RUNTIME_STATE_RUNNING:
            impl->state = UI_RUNTIME_STATE_SUSPEND_PENDING;
            ui_root_host_block_input(impl);
            return ui_result(UI_OK_TRANSITIONING, 0u);
        case UI_RUNTIME_STATE_SUSPEND_PENDING:
        case UI_RUNTIME_STATE_SUSPENDED:
            return ui_result(UI_OK_NO_CHANGE, 0u);
        case UI_RUNTIME_STATE_RESUME_PENDING:
            impl->state = UI_RUNTIME_STATE_SUSPENDED;
            return ui_result(UI_OK_NO_CHANGE, 0u);
        default:
            return ui_result(UI_ERR_NOT_INITIALIZED, 0u);
    }
}

ui_result_t ui_runtime_resume(ui_runtime_t *runtime)
{
    ui_runtime_impl_t *impl;
    ui_result_code_t result;

    if(runtime == NULL) return ui_result(UI_ERR_NOT_INITIALIZED, 0u);
    impl = ui_runtime_impl(runtime);
    result = basic_mutation_result(impl);
    if(result != UI_OK) return ui_result(result, 0u);

    switch(impl->state) {
        case UI_RUNTIME_STATE_RUNNING:
            return ui_result(UI_OK_NO_CHANGE, 0u);
        case UI_RUNTIME_STATE_SUSPEND_PENDING:
            impl->state = UI_RUNTIME_STATE_RUNNING;
            if(impl->txn.phase == UI_TXN_NONE) {
                ui_overlay_restore_input(impl);
            }
            return ui_result(UI_OK_NO_CHANGE, 0u);
        case UI_RUNTIME_STATE_SUSPENDED:
            impl->state = UI_RUNTIME_STATE_RESUME_PENDING;
            return ui_result(UI_OK_TRANSITIONING, 0u);
        case UI_RUNTIME_STATE_RESUME_PENDING:
            return ui_result(UI_OK_NO_CHANGE, 0u);
        default:
            return ui_result(UI_ERR_NOT_INITIALIZED, 0u);
    }
}

ui_result_t ui_nav_push(
    ui_runtime_t *runtime, uint16_t route_id, const ui_args_t *args,
    ui_entry_handle_t *out)
{
    return navigate_new(
        runtime, route_id, args, out, UI_NAV_OP_PUSH);
}

ui_result_t ui_nav_replace(
    ui_runtime_t *runtime, uint16_t route_id, const ui_args_t *args,
    ui_entry_handle_t *out)
{
    return navigate_new(
        runtime, route_id, args, out, UI_NAV_OP_REPLACE);
}

ui_result_t ui_nav_reset(
    ui_runtime_t *runtime, uint16_t route_id, const ui_args_t *args,
    ui_entry_handle_t *out)
{
    return navigate_new(
        runtime, route_id, args, out, UI_NAV_OP_RESET);
}

ui_result_t ui_nav_pop(ui_runtime_t *runtime)
{
    ui_runtime_impl_t *impl;
    ui_result_code_t result;

    if(runtime == NULL) return ui_result(UI_ERR_NOT_INITIALIZED, 0u);
    impl = ui_runtime_impl(runtime);
    result = admission_result(impl);
    if(result != UI_OK) return ui_result(result, 0u);
    result = ui_overlay_navigation_blocker(impl);
    if(result != UI_OK) return ui_result(result, 0u);
    if(impl->stack_depth <= 1u) return ui_result(UI_OK_NO_CHANGE, 0u);
    return navigate_existing_admitted(
        impl, (uint16_t)(impl->stack_depth - 2u),
        UI_NAV_OP_POP, UI_ACTIVATE_BACK, NULL, NULL);
}

ui_result_t ui_nav_pop_to(
    ui_runtime_t *runtime, ui_entry_handle_t target)
{
    ui_runtime_impl_t *impl;
    ui_result_code_t result;
    uint16_t position;

    if(runtime == NULL) return ui_result(UI_ERR_NOT_INITIALIZED, 0u);
    impl = ui_runtime_impl(runtime);
    result = admission_result(impl);
    if(result != UI_OK) return ui_result(result, 0u);
    result = ui_overlay_navigation_blocker(impl);
    if(result != UI_OK) return ui_result(result, 0u);
    if(!handle_is_valid_unchecked(impl, target)) {
        return ui_result(UI_ERR_INVALID_ENTRY, 0u);
    }
    position = stack_position_for_index(impl, target.slot);
    if(position == UI_INDEX_NONE) return ui_result(UI_ERR_INVALID_ENTRY, 0u);
    if(position == impl->stack_depth - 1u) {
        return ui_result(UI_OK_NO_CHANGE, 0u);
    }
    return navigate_existing_admitted(
        impl, position, UI_NAV_OP_POP_TO, UI_ACTIVATE_BACK,
        NULL, NULL);
}

ui_result_t ui_nav_back(ui_runtime_t *runtime)
{
    ui_runtime_impl_t *impl;
    ui_result_code_t result;
    ui_back_result_t back_result;
    ui_entry_t *active;

    if(runtime == NULL) return ui_result(UI_ERR_NOT_INITIALIZED, 0u);
    impl = ui_runtime_impl(runtime);
    result = admission_result(impl);
    if(result != UI_OK) return ui_result(result, 0u);

    result = ui_overlay_back(impl);
    if(result == UI_OK_NO_CHANGE) return ui_result(UI_OK_NO_CHANGE, 0u);
    if(result == UI_ERR_FAULTED) return ui_result(result, 0u);

    active = &impl->entries[top_index(impl)];
    back_result = call_back(impl, active);
    if(impl->state == UI_RUNTIME_STATE_FAULTED) {
        return ui_result(UI_ERR_FAULTED, 0u);
    }
    if(back_result == UI_BACK_RESULT_CONSUMED) {
        return ui_result(UI_OK_NO_CHANGE, 0u);
    }
    if(impl->stack_depth <= 1u) {
        return ui_result(UI_BACK_UNHANDLED, 0u);
    }
    return navigate_existing_admitted(
        impl, (uint16_t)(impl->stack_depth - 2u),
        UI_NAV_OP_POP, UI_ACTIVATE_BACK, NULL, NULL);
}

ui_result_t ui_entry_invalidate(
    ui_runtime_t *runtime, ui_entry_handle_t handle)
{
    ui_runtime_impl_t *impl;
    ui_result_code_t result;

    if(runtime == NULL) return ui_result(UI_ERR_NOT_INITIALIZED, 0u);
    impl = ui_runtime_impl(runtime);
    result = basic_mutation_result(impl);
    if(result != UI_OK) return ui_result(result, 0u);
    if(!handle_is_valid_unchecked(impl, handle)) {
        return ui_result(UI_ERR_INVALID_ENTRY, 0u);
    }
    impl->entries[handle.slot].dirty = true;
    return ui_result(UI_OK, 0u);
}

ui_result_t ui_entry_get_top(
    const ui_runtime_t *runtime, ui_entry_handle_t *out)
{
    const ui_runtime_impl_t *impl;

    clear_handle(out);
    if(runtime == NULL || out == NULL) {
        return ui_result(UI_ERR_INVALID_ARGUMENT, 0u);
    }
    impl = ui_runtime_impl_const(runtime);
    if(!impl_is_initialized(impl)) {
        return ui_result(UI_ERR_NOT_INITIALIZED, 0u);
    }
    if(impl->state == UI_RUNTIME_STATE_FAULTED) {
        return ui_result(UI_ERR_FAULTED, 0u);
    }
    if(!is_ui_thread(impl)) return ui_result(UI_ERR_WRONG_THREAD, 0u);
    if(impl->stack_depth == 0u) return ui_result(UI_ERR_HOST, 0u);
    *out = make_handle(impl, top_index(impl));
    return ui_result(UI_OK, 0u);
}

ui_result_t ui_entry_find_topmost(
    const ui_runtime_t *runtime, uint16_t route_id,
    ui_entry_handle_t *out)
{
    const ui_runtime_impl_t *impl;
    uint16_t i;

    clear_handle(out);
    if(runtime == NULL || out == NULL || route_id == 0u) {
        return ui_result(UI_ERR_INVALID_ARGUMENT, 0u);
    }
    impl = ui_runtime_impl_const(runtime);
    if(!impl_is_initialized(impl)) {
        return ui_result(UI_ERR_NOT_INITIALIZED, 0u);
    }
    if(impl->state == UI_RUNTIME_STATE_FAULTED) {
        return ui_result(UI_ERR_FAULTED, 0u);
    }
    if(!is_ui_thread(impl)) return ui_result(UI_ERR_WRONG_THREAD, 0u);
    if(find_route(impl, route_id) == NULL) {
        return ui_result(UI_ERR_UNKNOWN_ROUTE, 0u);
    }
    for(i = impl->stack_depth; i > 0u; --i) {
        uint16_t index = impl->stack[i - 1u];
        if(impl->entries[index].route->route_id == route_id) {
            *out = make_handle(impl, index);
            return ui_result(UI_OK, 0u);
        }
    }
    return ui_result(UI_ERR_INVALID_ENTRY, 0u);
}

bool ui_entry_is_valid(
    const ui_runtime_t *runtime, ui_entry_handle_t handle)
{
    const ui_runtime_impl_t *impl;
    if(runtime == NULL) return false;
    impl = ui_runtime_impl_const(runtime);
    if(!impl_is_initialized(impl) ||
       impl->state == UI_RUNTIME_STATE_FAULTED ||
       !is_ui_thread(impl)) {
        return false;
    }
    return handle_is_valid_unchecked(impl, handle);
}

ui_result_t ui_runtime_reclaim_views(
    ui_runtime_t *runtime, ui_reclaim_mode_t mode)
{
    ui_runtime_impl_t *impl;
    ui_result_code_t result;

    if(runtime == NULL) return ui_result(UI_ERR_NOT_INITIALIZED, 0u);
    impl = ui_runtime_impl(runtime);
    result = basic_mutation_result(impl);
    if(result != UI_OK) return ui_result(result, 0u);
    if(mode != UI_RECLAIM_ONE_OLDEST &&
       mode != UI_RECLAIM_ALL_HIDDEN) {
        return ui_result(UI_ERR_INVALID_ARGUMENT, 0u);
    }
    if(mode == UI_RECLAIM_ALL_HIDDEN || impl->pending_reclaim == 0u) {
        impl->pending_reclaim =
            mode == UI_RECLAIM_ALL_HIDDEN ? 2u : 1u;
    }
    return ui_result(UI_OK, 0u);
}
