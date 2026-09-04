#include "ui_runtime/ui_runtime.h"

#include <stdio.h>
#include <string.h>

#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

#define CHECK(expression)                                                   \
    do {                                                                    \
        if(!(expression)) {                                                 \
            fprintf(stderr, "%s:%d: check failed: %s\n",                  \
                    __FILE__, __LINE__, #expression);                       \
            return false;                                                   \
        }                                                                   \
    } while(0)

typedef struct page_probe page_probe_t;

typedef struct {
    page_probe_t *owner;
    lv_obj_t *slot_root;
    bool in_use;
    bool view_deleted;
} page_state_t;

struct page_probe {
    uint32_t create_count;
    uint32_t destroy_count;
    uint32_t mount_count;
    uint32_t unmount_count[4];
    uint32_t activate_count[4];
    uint32_t deactivate_count[3];
    uint32_t render_count;
    uint32_t back_count;
    bool fail_create;
    bool fail_mount;
    bool consume_back;
    bool register_focus;
    bool delete_slot_during_mount;
    bool delete_slot_during_unmount;
    bool delete_other_slot_on_child_delete;
    bool navigate_on_child_delete;
    bool callback_error;
    int last_reenter_value;
    ui_view_slot_t *last_slot;
    lv_obj_t *last_slot_root;
    lv_obj_t *last_focus_object;
    lv_obj_t *other_slot_root;
    ui_runtime_t *runtime;
    ui_result_t child_delete_nav_result;
    page_state_t states[UI_RUNTIME_MAX_DEPTH + 1u];
};

typedef struct {
    uint32_t mount_count;
    uint32_t unmount_count;
    uint32_t visible_count;
    uint32_t hidden_count;
    uint32_t style_count;
    uint16_t last_style;
    ui_status_visibility_reason_t last_visibility_reason;
    bool fail_mount;
    lv_obj_t *last_slot_root;
} status_probe_t;

typedef struct overlay_probe overlay_probe_t;

typedef struct {
    overlay_probe_t *owner;
    int id;
    int value;
    bool allocated;
    bool in_use;
    bool view_deleted;
    lv_obj_t *root;
    lv_obj_t *interactive_child;
    uint32_t mount_count;
    uint32_t unmount_count[3];
    uint32_t activate_count[4];
    uint32_t deactivate_count[3];
    uint32_t render_count;
    uint32_t update_count;
    uint32_t dismiss_count;
    ui_overlay_dismiss_reason_t dismiss_reason;
} overlay_state_t;

struct overlay_probe {
    overlay_state_t states[UI_RUNTIME_MAX_OVERLAYS + 1u];
    uint32_t create_count;
    uint32_t destroy_count;
    bool register_focus;
    bool enable_click_on_activate;
    int fail_mount_id;
    int fail_update_id;
    bool callback_error;
};

typedef struct fixture fixture_t;

typedef struct {
    ui_runtime_event_t events[64];
    uint16_t count;
    bool navigate_in_started;
    ui_result_t reentrant_result;
    fixture_t *fixture;
} observer_probe_t;

struct fixture {
    ui_runtime_t runtime;
    lv_disp_t *display;
    lv_indev_t *keypad;
    lv_indev_t *indevs[1];
    ui_page_adapter_t page_adapter;
    ui_route_desc_t routes[4];
    page_probe_t pages[4];
    ui_status_bar_adapter_t status_adapter;
    status_probe_t status;
    ui_overlay_adapter_t overlay_adapter;
    ui_overlay_desc_t overlay_descs[3];
    overlay_probe_t overlay_probes[3];
    observer_probe_t observer;
    ui_runtime_config_t config;
    bool thread_ok;
};

static void page_child_delete(lv_event_t *event);
static void overlay_child_delete(lv_event_t *event);

static ui_result_code_t page_create_state(
    void *ctx, const ui_args_t *args, void **out_state)
{
    page_probe_t *probe = (page_probe_t *)ctx;
    uint16_t i;
    (void)args;
    ++probe->create_count;
    if(probe->fail_create) return UI_ERR_STATE_CREATE;
    for(i = 0; i < ARRAY_COUNT(probe->states); ++i) {
        page_state_t *state = &probe->states[i];
        if(!state->in_use) {
            memset(state, 0, sizeof(*state));
            state->owner = probe;
            state->in_use = true;
            *out_state = state;
            return UI_OK;
        }
    }
    return UI_ERR_STATE_CREATE;
}

static void page_destroy_state(void *ctx, void *state)
{
    page_probe_t *probe = (page_probe_t *)ctx;
    page_state_t *page_state = (page_state_t *)state;
    if(page_state == NULL || page_state->owner != probe ||
       !page_state->in_use) {
        probe->callback_error = true;
    }
    else {
        if(page_state->slot_root != NULL && !page_state->view_deleted) {
            probe->callback_error = true;
        }
        page_state->in_use = false;
        page_state->slot_root = NULL;
    }
    ++probe->destroy_count;
}

static ui_result_code_t page_mount_view(
    void *ctx, ui_view_slot_t *slot, void *state)
{
    page_probe_t *probe = (page_probe_t *)ctx;
    page_state_t *page_state = (page_state_t *)state;
    lv_obj_t *root;
    lv_obj_t *sentinel;
    lv_obj_t *focus_object;
    ui_result_code_t focus_result;

    ++probe->mount_count;
    if(page_state == NULL || page_state->owner != probe ||
       !page_state->in_use) {
        probe->callback_error = true;
    }
    root = ui_view_slot_content(slot);
    if(root == NULL) {
        probe->callback_error = true;
        return UI_ERR_VIEW_MOUNT;
    }
    probe->last_slot = slot;
    probe->last_slot_root = root;
    if(page_state != NULL) {
        page_state->slot_root = root;
        page_state->view_deleted = false;
    }
    sentinel = lv_obj_create(root);
    if(sentinel == NULL) return UI_ERR_VIEW_MOUNT;

    if(lv_obj_add_event_cb(
           sentinel, page_child_delete,
           LV_EVENT_DELETE, page_state) == NULL) {
        return UI_ERR_VIEW_MOUNT;
    }

    if(probe->register_focus) {
        focus_object = lv_obj_create(root);
        if(focus_object == NULL) return UI_ERR_VIEW_MOUNT;
        probe->last_focus_object = focus_object;
        focus_result = ui_view_slot_focus_add(slot, focus_object);
        if(focus_result != UI_OK) return focus_result;
        focus_result = ui_view_slot_focus_initial(slot, focus_object);
        if(focus_result != UI_OK) return focus_result;
    }
    else {
        probe->last_focus_object = NULL;
    }
    if(probe->delete_slot_during_mount) lv_obj_del(root);
    return probe->fail_mount ? UI_ERR_VIEW_MOUNT : UI_OK;
}

static void page_unmount_view(
    void *ctx, ui_view_slot_t *slot, void *state,
    ui_unmount_reason_t reason)
{
    page_probe_t *probe = (page_probe_t *)ctx;
    page_state_t *page_state = (page_state_t *)state;
    if(page_state == NULL || page_state->owner != probe ||
       !page_state->in_use || ui_view_slot_content(slot) == NULL ||
       reason > 3) {
        probe->callback_error = true;
        return;
    }
    ++probe->unmount_count[reason];
    if(probe->delete_slot_during_unmount) {
        lv_obj_del(probe->last_slot_root);
        probe->last_slot_root = NULL;
    }
}

static void page_activate(
    void *ctx, ui_view_slot_t *slot, void *state,
    ui_activate_reason_t reason, const ui_args_t *reenter_args)
{
    page_probe_t *probe = (page_probe_t *)ctx;
    page_state_t *page_state = (page_state_t *)state;
    if(page_state == NULL || page_state->owner != probe ||
       !page_state->in_use || ui_view_slot_content(slot) == NULL ||
       reason > 3) {
        probe->callback_error = true;
        return;
    }
    ++probe->activate_count[reason];
    if(reason == UI_ACTIVATE_REENTER && reenter_args != NULL &&
       reenter_args->size == sizeof(int) && reenter_args->data != NULL) {
        memcpy(&probe->last_reenter_value,
               reenter_args->data, sizeof(int));
    }
}

static void page_deactivate(
    void *ctx, ui_view_slot_t *slot, void *state,
    ui_deactivate_reason_t reason)
{
    page_probe_t *probe = (page_probe_t *)ctx;
    page_state_t *page_state = (page_state_t *)state;
    if(page_state == NULL || page_state->owner != probe ||
       !page_state->in_use || ui_view_slot_content(slot) == NULL ||
       reason > 2) {
        probe->callback_error = true;
        return;
    }
    ++probe->deactivate_count[reason];
}

static void page_render(void *ctx, ui_view_slot_t *slot, void *state)
{
    page_probe_t *probe = (page_probe_t *)ctx;
    page_state_t *page_state = (page_state_t *)state;
    if(page_state == NULL || page_state->owner != probe ||
       !page_state->in_use || ui_view_slot_content(slot) == NULL) {
        probe->callback_error = true;
        return;
    }
    ++probe->render_count;
}

static ui_back_result_t page_back(
    void *ctx, ui_view_slot_t *slot, void *state)
{
    page_probe_t *probe = (page_probe_t *)ctx;
    page_state_t *page_state = (page_state_t *)state;
    if(page_state == NULL || page_state->owner != probe ||
       !page_state->in_use || ui_view_slot_content(slot) == NULL) {
        probe->callback_error = true;
        return UI_BACK_RESULT_CONSUMED;
    }
    ++probe->back_count;
    return probe->consume_back
               ? UI_BACK_RESULT_CONSUMED
               : UI_BACK_RESULT_UNHANDLED;
}

static ui_result_code_t status_mount(void *ctx, ui_view_slot_t *slot)
{
    status_probe_t *probe = (status_probe_t *)ctx;
    lv_obj_t *root = ui_view_slot_content(slot);
    ++probe->mount_count;
    probe->last_slot_root = root;
    if(root == NULL || lv_obj_create(root) == NULL) {
        return UI_ERR_VIEW_MOUNT;
    }
    return probe->fail_mount ? UI_ERR_VIEW_MOUNT : UI_OK;
}

static void status_unmount(void *ctx, ui_view_slot_t *slot)
{
    status_probe_t *probe = (status_probe_t *)ctx;
    if(ui_view_slot_content(slot) == NULL) return;
    ++probe->unmount_count;
}

static void status_set_visible(
    void *ctx, bool visible, ui_status_visibility_reason_t reason)
{
    status_probe_t *probe = (status_probe_t *)ctx;
    if(visible) ++probe->visible_count;
    else ++probe->hidden_count;
    probe->last_visibility_reason = reason;
}

static void status_apply_style(void *ctx, uint16_t style)
{
    status_probe_t *probe = (status_probe_t *)ctx;
    ++probe->style_count;
    probe->last_style = style;
}

static bool read_overlay_args(
    const ui_args_t *args, int *out_id, int *out_value)
{
    const int *values;
    if(args == NULL || args->data == NULL ||
       args->size != (uint16_t)(sizeof(int) * 2u)) {
        return false;
    }
    values = (const int *)args->data;
    *out_id = values[0];
    *out_value = values[1];
    return true;
}

static ui_result_code_t overlay_create_state(
    void *ctx, const ui_args_t *args, void **out_state)
{
    overlay_probe_t *probe = (overlay_probe_t *)ctx;
    int id;
    int value;
    uint16_t i;

    ++probe->create_count;
    if(!read_overlay_args(args, &id, &value)) return UI_ERR_STATE_CREATE;
    for(i = 0; i < ARRAY_COUNT(probe->states); ++i) {
        overlay_state_t *state = &probe->states[i];
        if(!state->in_use) {
            memset(state, 0, sizeof(*state));
            state->owner = probe;
            state->id = id;
            state->value = value;
            state->allocated = true;
            state->in_use = true;
            *out_state = state;
            return UI_OK;
        }
    }
    return UI_ERR_STATE_CREATE;
}

static void overlay_destroy_state(void *ctx, void *state_value)
{
    overlay_probe_t *probe = (overlay_probe_t *)ctx;
    overlay_state_t *state = (overlay_state_t *)state_value;

    if(state == NULL || state->owner != probe || !state->in_use ||
       (state->root != NULL && !state->view_deleted)) {
        probe->callback_error = true;
    }
    else {
        state->in_use = false;
    }
    ++probe->destroy_count;
}

static ui_result_code_t overlay_mount_view(
    void *ctx, ui_view_slot_t *slot, void *state_value)
{
    overlay_probe_t *probe = (overlay_probe_t *)ctx;
    overlay_state_t *state = (overlay_state_t *)state_value;
    lv_obj_t *root;
    lv_obj_t *sentinel;

    if(state == NULL || state->owner != probe || !state->in_use) {
        probe->callback_error = true;
        return UI_ERR_VIEW_MOUNT;
    }
    ++state->mount_count;
    root = ui_view_slot_content(slot);
    if(root == NULL) {
        probe->callback_error = true;
        return UI_ERR_VIEW_MOUNT;
    }
    state->root = root;
    state->view_deleted = false;
    sentinel = lv_obj_create(root);
    if(sentinel == NULL || lv_obj_add_event_cb(
        sentinel, overlay_child_delete, LV_EVENT_DELETE, state) == NULL) {
        return UI_ERR_VIEW_MOUNT;
    }
    state->interactive_child = lv_obj_create(root);
    if(state->interactive_child == NULL) return UI_ERR_VIEW_MOUNT;
    if(probe->register_focus) {
        if(ui_view_slot_focus_add(slot, state->interactive_child) != UI_OK ||
           ui_view_slot_focus_initial(slot, state->interactive_child) !=
               UI_OK) {
            return UI_ERR_VIEW_MOUNT;
        }
    }
    return state->id == probe->fail_mount_id
               ? UI_ERR_VIEW_MOUNT
               : UI_OK;
}

static void overlay_unmount_view(
    void *ctx, ui_view_slot_t *slot, void *state_value,
    ui_overlay_unmount_reason_t reason)
{
    overlay_probe_t *probe = (overlay_probe_t *)ctx;
    overlay_state_t *state = (overlay_state_t *)state_value;
    if(state == NULL || state->owner != probe || !state->in_use ||
       ui_view_slot_content(slot) == NULL || reason > 2) {
        probe->callback_error = true;
        return;
    }
    ++state->unmount_count[reason];
}

static void overlay_activate(
    void *ctx, ui_view_slot_t *slot, void *state_value,
    ui_overlay_activate_reason_t reason)
{
    overlay_probe_t *probe = (overlay_probe_t *)ctx;
    overlay_state_t *state = (overlay_state_t *)state_value;
    if(state == NULL || state->owner != probe || !state->in_use ||
       ui_view_slot_content(slot) == NULL || reason > 3) {
        probe->callback_error = true;
        return;
    }
    ++state->activate_count[reason];
    if(probe->enable_click_on_activate && state->interactive_child != NULL) {
        lv_obj_add_flag(state->interactive_child, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void overlay_deactivate(
    void *ctx, ui_view_slot_t *slot, void *state_value,
    ui_overlay_deactivate_reason_t reason)
{
    overlay_probe_t *probe = (overlay_probe_t *)ctx;
    overlay_state_t *state = (overlay_state_t *)state_value;
    if(state == NULL || state->owner != probe || !state->in_use ||
       ui_view_slot_content(slot) == NULL || reason > 2) {
        probe->callback_error = true;
        return;
    }
    ++state->deactivate_count[reason];
}

static void overlay_render(
    void *ctx, ui_view_slot_t *slot, void *state_value)
{
    overlay_probe_t *probe = (overlay_probe_t *)ctx;
    overlay_state_t *state = (overlay_state_t *)state_value;
    if(state == NULL || state->owner != probe || !state->in_use ||
       ui_view_slot_content(slot) == NULL) {
        probe->callback_error = true;
        return;
    }
    ++state->render_count;
}

static ui_result_code_t overlay_update_state(
    void *ctx, void *state_value, const ui_args_t *args)
{
    overlay_probe_t *probe = (overlay_probe_t *)ctx;
    overlay_state_t *state = (overlay_state_t *)state_value;
    int id;
    int value;
    if(state == NULL || state->owner != probe || !state->in_use ||
       !read_overlay_args(args, &id, &value) || id != state->id) {
        probe->callback_error = true;
        return UI_ERR_INVALID_ARGUMENT;
    }
    if(id == probe->fail_update_id) return UI_ERR_INVALID_ARGUMENT;
    state->value = value;
    ++state->update_count;
    return UI_OK;
}

static void overlay_dismissed(
    void *ctx, void *state_value, ui_overlay_dismiss_reason_t reason)
{
    overlay_probe_t *probe = (overlay_probe_t *)ctx;
    overlay_state_t *state = (overlay_state_t *)state_value;
    if(state == NULL || state->owner != probe || !state->in_use ||
       reason > UI_OVERLAY_DISMISS_ACTIVATION_FAILED) {
        probe->callback_error = true;
        return;
    }
    ++state->dismiss_count;
    state->dismiss_reason = reason;
}

static void overlay_child_delete(lv_event_t *event)
{
    overlay_state_t *state =
        (overlay_state_t *)lv_event_get_user_data(event);
    if(lv_event_get_code(event) == LV_EVENT_DELETE) {
        state->view_deleted = true;
    }
}

static overlay_state_t *find_overlay_state(
    overlay_probe_t *probe, int id)
{
    uint16_t i;
    for(i = 0; i < ARRAY_COUNT(probe->states); ++i) {
        if(probe->states[i].allocated && probe->states[i].id == id) {
            return &probe->states[i];
        }
    }
    return NULL;
}

static bool thread_check(void *ctx)
{
    return *(bool *)ctx;
}

static void runtime_observer(void *ctx, const ui_runtime_event_t *event)
{
    observer_probe_t *probe = (observer_probe_t *)ctx;
    if(probe->count < ARRAY_COUNT(probe->events)) {
        probe->events[probe->count++] = *event;
    }
    if(probe->navigate_in_started && event->kind == UI_NAV_STARTED) {
        probe->reentrant_result = ui_nav_pop(&probe->fixture->runtime);
    }
}

static void fixture_prepare(
    fixture_t *fixture, uint16_t route_count,
    uint16_t max_depth, bool with_status)
{
    uint16_t i;

    memset(fixture, 0, sizeof(*fixture));
    fixture->thread_ok = true;
    fixture->display = lv_test_display_create(800, 480);
    fixture->keypad = lv_test_indev_create(
        fixture->display, LV_INDEV_TYPE_KEYPAD);
    fixture->indevs[0] = fixture->keypad;

    fixture->page_adapter.abi_version = UI_RUNTIME_ABI_VERSION;
    fixture->page_adapter.struct_size =
        (uint16_t)sizeof(fixture->page_adapter);
    fixture->page_adapter.create_state = page_create_state;
    fixture->page_adapter.destroy_state = page_destroy_state;
    fixture->page_adapter.mount_view = page_mount_view;
    fixture->page_adapter.unmount_view = page_unmount_view;
    fixture->page_adapter.on_activate = page_activate;
    fixture->page_adapter.on_deactivate = page_deactivate;
    fixture->page_adapter.render = page_render;
    fixture->page_adapter.on_back = page_back;

    for(i = 0; i < route_count; ++i) {
        fixture->routes[i].abi_version = UI_RUNTIME_ABI_VERSION;
        fixture->routes[i].struct_size =
            (uint16_t)sizeof(fixture->routes[i]);
        fixture->routes[i].route_id = (uint16_t)(i + 1u);
        fixture->routes[i].status_style = (uint16_t)(10u + i);
        fixture->routes[i].host_kind = UI_HOST_SHELL;
        fixture->routes[i].view_policy = UI_VIEW_REBUILD;
        fixture->routes[i].launch_mode = UI_LAUNCH_STANDARD;
        fixture->routes[i].adapter = &fixture->page_adapter;
        fixture->routes[i].user_ctx = &fixture->pages[i];
    }

    fixture->status_adapter.abi_version = UI_RUNTIME_ABI_VERSION;
    fixture->status_adapter.struct_size =
        (uint16_t)sizeof(fixture->status_adapter);
    fixture->status_adapter.mount = status_mount;
    fixture->status_adapter.unmount = status_unmount;
    fixture->status_adapter.set_visible = status_set_visible;
    fixture->status_adapter.apply_style = status_apply_style;

    fixture->observer.fixture = fixture;
    for(i = 0; i < route_count; ++i) {
        fixture->pages[i].runtime = &fixture->runtime;
    }
    fixture->config.abi_version = UI_RUNTIME_ABI_VERSION;
    fixture->config.struct_size = (uint16_t)sizeof(fixture->config);
    fixture->config.routes = fixture->routes;
    fixture->config.route_count = route_count;
    fixture->config.initial_route_id = 1u;
    fixture->config.max_depth = max_depth;
    fixture->config.display = fixture->display;
    fixture->config.indevs = fixture->indevs;
    fixture->config.indev_count = 1u;
    fixture->config.observer = runtime_observer;
    fixture->config.observer_ctx = &fixture->observer;
    fixture->config.is_ui_thread = thread_check;
    fixture->config.thread_ctx = &fixture->thread_ok;
    if(with_status) {
        fixture->config.status_bar = &fixture->status_adapter;
        fixture->config.status_bar_ctx = &fixture->status;
        fixture->config.status_bar_height = 40;
    }
}

static void fixture_enable_overlays(fixture_t *fixture)
{
    uint16_t i;

    fixture->overlay_adapter.abi_version = UI_RUNTIME_ABI_VERSION;
    fixture->overlay_adapter.struct_size =
        (uint16_t)sizeof(fixture->overlay_adapter);
    fixture->overlay_adapter.create_state = overlay_create_state;
    fixture->overlay_adapter.destroy_state = overlay_destroy_state;
    fixture->overlay_adapter.mount_view = overlay_mount_view;
    fixture->overlay_adapter.unmount_view = overlay_unmount_view;
    fixture->overlay_adapter.on_activate = overlay_activate;
    fixture->overlay_adapter.on_deactivate = overlay_deactivate;
    fixture->overlay_adapter.render = overlay_render;
    fixture->overlay_adapter.update_state = overlay_update_state;
    fixture->overlay_adapter.on_dismiss = overlay_dismissed;

    for(i = 0; i < ARRAY_COUNT(fixture->overlay_descs); ++i) {
        fixture->overlay_descs[i].abi_version = UI_RUNTIME_ABI_VERSION;
        fixture->overlay_descs[i].struct_size =
            (uint16_t)sizeof(fixture->overlay_descs[i]);
        fixture->overlay_descs[i].overlay_type = (uint16_t)(101u + i);
        fixture->overlay_descs[i].adapter = &fixture->overlay_adapter;
        fixture->overlay_descs[i].user_ctx = &fixture->overlay_probes[i];
    }

    fixture->overlay_descs[0].lane = UI_OVERLAY_LANE_MODAL;
    fixture->overlay_descs[0].pointer_interactive = true;
    fixture->overlay_descs[0].modal_back_policy = UI_MODAL_BACK_DISMISS;
    fixture->overlay_probes[0].register_focus = true;

    fixture->overlay_descs[1].lane = UI_OVERLAY_LANE_NOTICE;
    fixture->overlay_descs[1].pointer_interactive = false;
    fixture->overlay_descs[1].modal_back_policy = UI_MODAL_BACK_DISMISS;

    fixture->overlay_descs[2].lane = UI_OVERLAY_LANE_TRANSIENT;
    fixture->overlay_descs[2].pointer_interactive = false;
    fixture->overlay_descs[2].modal_back_policy = UI_MODAL_BACK_DISMISS;
    fixture->overlay_descs[2].default_transient_ms = 100u;

    fixture->config.overlays = fixture->overlay_descs;
    fixture->config.overlay_type_count =
        (uint16_t)ARRAY_COUNT(fixture->overlay_descs);
    fixture->config.modal_capacity = 2u;
    fixture->config.notice_capacity = 3u;
    fixture->config.transient_capacity = 1u;
}

static ui_overlay_request_t overlay_request(
    uint32_t key, int16_t priority, int id, int value)
{
    static int values[32][2];
    static uint8_t cursor;
    ui_overlay_request_t request;

    cursor = (uint8_t)((cursor + 1u) % ARRAY_COUNT(values));
    values[cursor][0] = id;
    values[cursor][1] = value;
    memset(&request, 0, sizeof(request));
    request.key = key;
    request.priority = priority;
    request.owner_kind = UI_OVERLAY_OWNER_RUNTIME;
    request.args.data = values[cursor];
    request.args.size = (uint16_t)sizeof(values[cursor]);
    return request;
}

static bool all_callbacks_valid(const fixture_t *fixture, uint16_t count)
{
    uint16_t i;
    for(i = 0; i < count; ++i) {
        if(fixture->pages[i].callback_error) return false;
    }
    return true;
}

static bool test_init_invalidate_and_root_back(void)
{
    fixture_t fixture;
    ui_result_t result;
    ui_entry_handle_t top;
    lv_obj_t *bootstrap;
    size_t before_init;

    fixture_prepare(&fixture, 2u, 3u, true);
    fixture.pages[0].register_focus = true;
    bootstrap = lv_obj_create(NULL);
    CHECK(bootstrap != NULL);
    lv_disp_load_scr(bootstrap);
    before_init = lv_test_object_count();

    result = ui_runtime_init(&fixture.runtime, &fixture.config);
    CHECK(result.code == UI_OK && result.txn_id == 0u);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_RUNNING);
    CHECK(lv_test_display_active_screen(fixture.display) != bootstrap);
    CHECK(lv_test_object_count() > before_init);
    CHECK(fixture.pages[0].create_count == 1u);
    CHECK(fixture.pages[0].mount_count == 1u);
    CHECK(fixture.pages[0].activate_count[UI_ACTIVATE_NEW] == 1u);
    CHECK(fixture.pages[0].render_count == 1u);
    CHECK(fixture.status.mount_count == 1u);
    CHECK(fixture.status.visible_count == 1u);
    CHECK(fixture.status.last_style == 10u);
    CHECK(lv_test_indev_group(fixture.keypad) != NULL);
    CHECK(ui_view_slot_content(fixture.pages[0].last_slot) == NULL);

    result = ui_entry_get_top(&fixture.runtime, &top);
    CHECK(result.code == UI_OK && ui_entry_is_valid(&fixture.runtime, top));
    CHECK(ui_entry_invalidate(&fixture.runtime, top).code == UI_OK);
    CHECK(ui_entry_invalidate(&fixture.runtime, top).code == UI_OK);
    CHECK(fixture.pages[0].render_count == 1u);
    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(fixture.pages[0].render_count == 2u);
    CHECK(ui_nav_back(&fixture.runtime).code == UI_BACK_UNHANDLED);
    CHECK(fixture.pages[0].back_count == 1u);
    CHECK(all_callbacks_valid(&fixture, 2u));
    lv_test_reset();
    return true;
}

static bool test_push_rebuild_then_pop(void)
{
    fixture_t fixture;
    ui_entry_handle_t first;
    ui_entry_handle_t second;
    ui_entry_handle_t top;
    ui_result_t result;

    fixture_prepare(&fixture, 3u, 3u, false);
    fixture.pages[0].register_focus = true;
    fixture.pages[1].register_focus = true;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(ui_entry_get_top(&fixture.runtime, &first).code == UI_OK);

    result = ui_nav_push(&fixture.runtime, 2u, NULL, &second);
    CHECK(result.code == UI_OK_TRANSITIONING && result.txn_id != 0u);
    CHECK(ui_entry_is_valid(&fixture.runtime, first));
    CHECK(ui_entry_is_valid(&fixture.runtime, second));
    CHECK(fixture.observer.count == 1u);
    CHECK(fixture.observer.events[0].kind == UI_NAV_STARTED);
    CHECK(!fixture.observer.events[0].to_handle_valid);
    CHECK(fixture.pages[0].deactivate_count[UI_DEACTIVATE_COVERED] == 1u);
    CHECK(fixture.pages[0].unmount_count[UI_UNMOUNT_REBUILD] == 0u);
    CHECK(ui_nav_push(&fixture.runtime, 3u, NULL, NULL).code == UI_ERR_BUSY);

    ui_runtime_tick(&fixture.runtime, 10u);
    CHECK(fixture.pages[0].unmount_count[UI_UNMOUNT_REBUILD] == 1u);
    CHECK(fixture.pages[0].destroy_count == 0u);
    CHECK(fixture.observer.count == 2u);
    CHECK(fixture.observer.events[1].kind == UI_NAV_COMPLETED);
    CHECK(fixture.observer.events[1].from_handle_valid);

    result = ui_nav_pop(&fixture.runtime);
    CHECK(result.code == UI_OK_TRANSITIONING);
    CHECK(fixture.pages[0].mount_count == 2u);
    CHECK(fixture.pages[0].activate_count[UI_ACTIVATE_BACK] == 1u);
    CHECK(fixture.pages[1].deactivate_count[UI_DEACTIVATE_REMOVED] == 1u);
    CHECK(!ui_entry_is_valid(&fixture.runtime, second));
    CHECK(fixture.pages[1].destroy_count == 0u);
    CHECK(ui_entry_get_top(&fixture.runtime, &top).code == UI_OK);
    CHECK(top.entry_generation == first.entry_generation &&
          top.slot == first.slot);

    ui_runtime_tick(&fixture.runtime, 20u);
    CHECK(fixture.pages[1].unmount_count[UI_UNMOUNT_REMOVED] == 1u);
    CHECK(fixture.pages[1].destroy_count == 1u);
    CHECK(fixture.pages[0].destroy_count == 0u);
    CHECK(lv_test_indev_group(fixture.keypad) != NULL);
    CHECK(all_callbacks_valid(&fixture, 3u));
    lv_test_reset();
    return true;
}

static bool test_retain_reclaim_and_restore(void)
{
    fixture_t fixture;
    ui_entry_handle_t first;

    fixture_prepare(&fixture, 2u, 3u, false);
    fixture.routes[0].view_policy = UI_VIEW_RETAIN;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(ui_entry_get_top(&fixture.runtime, &first).code == UI_OK);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, NULL).code ==
          UI_OK_TRANSITIONING);
    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(fixture.pages[0].mount_count == 1u);
    CHECK(fixture.pages[0].unmount_count[UI_UNMOUNT_REBUILD] == 0u);

    CHECK(ui_runtime_reclaim_views(
              &fixture.runtime, UI_RECLAIM_ONE_OLDEST).code == UI_OK);
    CHECK(fixture.pages[0].unmount_count[UI_UNMOUNT_RECLAIM] == 0u);
    ui_runtime_tick(&fixture.runtime, 2u);
    CHECK(fixture.pages[0].unmount_count[UI_UNMOUNT_RECLAIM] == 1u);
    CHECK(fixture.pages[0].destroy_count == 0u);
    CHECK(ui_entry_is_valid(&fixture.runtime, first));

    CHECK(ui_nav_pop(&fixture.runtime).code == UI_OK_TRANSITIONING);
    CHECK(fixture.pages[0].mount_count == 2u);
    ui_runtime_tick(&fixture.runtime, 3u);
    CHECK(fixture.pages[1].destroy_count == 1u);
    CHECK(all_callbacks_valid(&fixture, 2u));
    lv_test_reset();
    return true;
}

static bool test_mount_failure_rolls_back_at_tick(void)
{
    fixture_t fixture;
    ui_entry_handle_t first;
    ui_entry_handle_t output;
    ui_entry_handle_t top;
    ui_result_t result;
    size_t stable_objects;

    fixture_prepare(&fixture, 2u, 3u, false);
    fixture.pages[1].fail_mount = true;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(ui_entry_get_top(&fixture.runtime, &first).code == UI_OK);
    stable_objects = lv_test_object_count();
    memset(&output, 0xA5, sizeof(output));

    result = ui_nav_push(&fixture.runtime, 2u, NULL, &output);
    CHECK(result.code == UI_ERR_VIEW_MOUNT && result.txn_id != 0u);
    CHECK(output.runtime_generation == 0u);
    CHECK(fixture.pages[1].create_count == 1u);
    CHECK(fixture.pages[1].mount_count == 1u);
    CHECK(fixture.pages[1].destroy_count == 0u);
    CHECK(fixture.pages[1].unmount_count[UI_UNMOUNT_REMOVED] == 0u);
    CHECK(ui_nav_pop(&fixture.runtime).code == UI_ERR_BUSY);
    CHECK(ui_entry_get_top(&fixture.runtime, &top).code == UI_OK);
    CHECK(top.slot == first.slot &&
          top.entry_generation == first.entry_generation);

    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(fixture.pages[1].destroy_count == 1u);
    CHECK(fixture.pages[1].unmount_count[UI_UNMOUNT_REMOVED] == 0u);
    CHECK(lv_test_object_count() == stable_objects);
    CHECK(fixture.observer.count == 2u);
    CHECK(fixture.observer.events[1].kind == UI_NAV_FAILED);
    CHECK(fixture.observer.events[1].code == UI_ERR_VIEW_MOUNT);
    CHECK(ui_entry_is_valid(&fixture.runtime, first));

    fixture.pages[1].fail_mount = false;
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, NULL).code ==
          UI_OK_TRANSITIONING);
    ui_runtime_tick(&fixture.runtime, 2u);
    CHECK(all_callbacks_valid(&fixture, 2u));
    lv_test_reset();
    return true;
}

static bool test_existing_target_mount_failure_preserves_entry(void)
{
    fixture_t fixture;
    ui_entry_handle_t first;
    ui_entry_handle_t second;
    ui_entry_handle_t top;
    ui_result_t result;

    fixture_prepare(&fixture, 2u, 2u, false);
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(ui_entry_get_top(&fixture.runtime, &first).code == UI_OK);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, &second).code ==
          UI_OK_TRANSITIONING);
    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(fixture.pages[0].unmount_count[UI_UNMOUNT_REBUILD] == 1u);

    fixture.pages[0].fail_mount = true;
    result = ui_nav_pop(&fixture.runtime);
    CHECK(result.code == UI_ERR_VIEW_MOUNT && result.txn_id != 0u);
    CHECK(ui_entry_is_valid(&fixture.runtime, first));
    CHECK(ui_entry_is_valid(&fixture.runtime, second));
    CHECK(ui_entry_get_top(&fixture.runtime, &top).code == UI_OK);
    CHECK(top.slot == second.slot &&
          top.entry_generation == second.entry_generation);
    CHECK(fixture.pages[0].create_count == 1u);
    CHECK(fixture.pages[0].destroy_count == 0u);
    CHECK(fixture.pages[0].mount_count == 2u);
    CHECK(fixture.pages[0].unmount_count[UI_UNMOUNT_REMOVED] == 0u);

    ui_runtime_tick(&fixture.runtime, 2u);
    CHECK(fixture.observer.events[3].kind == UI_NAV_FAILED);
    CHECK(fixture.pages[0].destroy_count == 0u);
    fixture.pages[0].fail_mount = false;
    CHECK(ui_nav_pop(&fixture.runtime).code == UI_OK_TRANSITIONING);
    ui_runtime_tick(&fixture.runtime, 3u);
    CHECK(fixture.pages[0].mount_count == 3u);
    CHECK(fixture.pages[1].destroy_count == 1u);
    CHECK(all_callbacks_valid(&fixture, 2u));
    lv_test_reset();
    return true;
}

static bool test_create_failure_does_not_destroy_unowned_state(void)
{
    fixture_t fixture;
    ui_result_t result;

    fixture_prepare(&fixture, 2u, 3u, false);
    fixture.pages[1].fail_create = true;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    result = ui_nav_push(&fixture.runtime, 2u, NULL, NULL);
    CHECK(result.code == UI_ERR_STATE_CREATE && result.txn_id != 0u);
    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(fixture.pages[1].create_count == 1u);
    CHECK(fixture.pages[1].mount_count == 0u);
    CHECK(fixture.pages[1].destroy_count == 0u);
    CHECK(fixture.observer.events[1].kind == UI_NAV_FAILED);
    lv_test_reset();
    return true;
}

static bool test_full_stack_still_allows_replace_and_reset(void)
{
    fixture_t fixture;
    ui_entry_handle_t root;
    ui_entry_handle_t second;
    ui_entry_handle_t third;

    fixture_prepare(&fixture, 3u, 2u, false);
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(ui_entry_get_top(&fixture.runtime, &root).code == UI_OK);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, &second).code ==
          UI_OK_TRANSITIONING);
    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(ui_nav_push(&fixture.runtime, 3u, NULL, NULL).code ==
          UI_ERR_BACK_STACK_FULL);

    CHECK(ui_nav_replace(&fixture.runtime, 3u, NULL, &third).code ==
          UI_OK_TRANSITIONING);
    CHECK(!ui_entry_is_valid(&fixture.runtime, second));
    CHECK(ui_entry_is_valid(&fixture.runtime, root));
    ui_runtime_tick(&fixture.runtime, 2u);
    CHECK(fixture.pages[1].destroy_count == 1u);

    CHECK(ui_nav_reset(&fixture.runtime, 2u, NULL, &second).code ==
          UI_OK_TRANSITIONING);
    CHECK(!ui_entry_is_valid(&fixture.runtime, root));
    CHECK(!ui_entry_is_valid(&fixture.runtime, third));
    ui_runtime_tick(&fixture.runtime, 3u);
    CHECK(ui_nav_pop(&fixture.runtime).code == UI_OK_NO_CHANGE);
    CHECK(ui_nav_back(&fixture.runtime).code == UI_BACK_UNHANDLED);
    CHECK(all_callbacks_valid(&fixture, 3u));
    lv_test_reset();
    return true;
}

static bool test_pop_to_single_top_and_single_task(void)
{
    fixture_t fixture;
    ui_entry_handle_t root;
    ui_entry_handle_t second;
    ui_entry_handle_t third;
    ui_entry_handle_t output;
    ui_args_t args;
    ui_result_t result;
    int value = 73;
    uint16_t event_count;

    fixture_prepare(&fixture, 3u, 3u, false);
    fixture.routes[0].launch_mode = UI_LAUNCH_SINGLE_TASK;
    fixture.routes[1].launch_mode = UI_LAUNCH_SINGLE_TOP;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(ui_entry_get_top(&fixture.runtime, &root).code == UI_OK);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, &second).code ==
          UI_OK_TRANSITIONING);
    ui_runtime_tick(&fixture.runtime, 1u);

    args.data = &value;
    args.size = (uint16_t)sizeof(value);
    event_count = fixture.observer.count;
    result = ui_nav_push(&fixture.runtime, 2u, &args, &output);
    CHECK(result.code == UI_OK_REENTERED && result.txn_id == 0u);
    CHECK(output.slot == second.slot &&
          output.entry_generation == second.entry_generation);
    CHECK(fixture.observer.count == event_count);
    CHECK(fixture.pages[1].activate_count[UI_ACTIVATE_REENTER] == 1u);
    CHECK(fixture.pages[1].last_reenter_value == value);
    CHECK(fixture.pages[1].render_count == 1u);
    ui_runtime_tick(&fixture.runtime, 2u);
    CHECK(fixture.pages[1].render_count == 2u);

    CHECK(ui_nav_push(&fixture.runtime, 3u, NULL, &third).code ==
          UI_OK_TRANSITIONING);
    ui_runtime_tick(&fixture.runtime, 3u);
    CHECK(ui_nav_pop_to(&fixture.runtime, second).code ==
          UI_OK_TRANSITIONING);
    CHECK(!ui_entry_is_valid(&fixture.runtime, third));
    ui_runtime_tick(&fixture.runtime, 4u);

    result = ui_nav_push(&fixture.runtime, 1u, &args, &output);
    CHECK(result.code == UI_OK_TRANSITIONING);
    CHECK(output.slot == root.slot &&
          output.entry_generation == root.entry_generation);
    CHECK(!ui_entry_is_valid(&fixture.runtime, second));
    CHECK(fixture.pages[0].activate_count[UI_ACTIVATE_REENTER] == 1u);
    CHECK(fixture.pages[0].last_reenter_value == value);
    ui_runtime_tick(&fixture.runtime, 5u);
    CHECK(all_callbacks_valid(&fixture, 3u));
    lv_test_reset();
    return true;
}

static bool test_suspend_during_transaction_and_resume(void)
{
    fixture_t fixture;
    ui_entry_handle_t second;
    uint16_t event_count;

    fixture_prepare(&fixture, 2u, 3u, true);
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, &second).code ==
          UI_OK_TRANSITIONING);
    CHECK(ui_runtime_suspend(&fixture.runtime).code == UI_OK_TRANSITIONING);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_SUSPEND_PENDING);
    CHECK(ui_nav_pop(&fixture.runtime).code == UI_ERR_SUSPENDED);

    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_SUSPENDED);
    CHECK(fixture.pages[1].deactivate_count[
              UI_DEACTIVATE_DISPLAY_SUSPEND] == 1u);
    CHECK(fixture.observer.count == 3u);
    CHECK(fixture.observer.events[1].kind == UI_NAV_COMPLETED);
    CHECK(fixture.observer.events[2].kind == UI_RUNTIME_SUSPENDED);
    CHECK(fixture.status.hidden_count == 1u);

    CHECK(ui_entry_invalidate(&fixture.runtime, second).code == UI_OK);
    ui_runtime_tick(&fixture.runtime, 2u);
    CHECK(fixture.pages[1].render_count == 1u);
    CHECK(ui_runtime_resume(&fixture.runtime).code == UI_OK_TRANSITIONING);
    ui_runtime_tick(&fixture.runtime, 3u);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_RUNNING);
    CHECK(fixture.pages[1].activate_count[
              UI_ACTIVATE_DISPLAY_RESUME] == 1u);
    CHECK(fixture.pages[1].render_count == 2u);
    CHECK(fixture.status.visible_count == 2u);
    CHECK(fixture.status.last_visibility_reason ==
          UI_STATUS_VISIBILITY_DISPLAY_RESUME);

    event_count = fixture.observer.count;
    CHECK(ui_runtime_suspend(&fixture.runtime).code == UI_OK_TRANSITIONING);
    CHECK(ui_runtime_resume(&fixture.runtime).code == UI_OK_NO_CHANGE);
    ui_runtime_tick(&fixture.runtime, 4u);
    CHECK(fixture.observer.count == event_count);
    CHECK(fixture.pages[1].deactivate_count[
              UI_DEACTIVATE_DISPLAY_SUSPEND] == 1u);
    CHECK(all_callbacks_valid(&fixture, 2u));
    lv_test_reset();
    return true;
}

static bool test_fullscreen_controls_status_visibility(void)
{
    fixture_t fixture;

    fixture_prepare(&fixture, 2u, 2u, true);
    fixture.routes[1].host_kind = UI_HOST_FULLSCREEN;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(fixture.status.visible_count == 1u);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, NULL).code ==
          UI_OK_TRANSITIONING);
    CHECK(fixture.status.hidden_count == 1u);
    CHECK(lv_obj_has_flag(
        fixture.status.last_slot_root, LV_OBJ_FLAG_HIDDEN));
    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(ui_nav_pop(&fixture.runtime).code == UI_OK_TRANSITIONING);
    CHECK(fixture.status.visible_count == 2u);
    CHECK(!lv_obj_has_flag(
        fixture.status.last_slot_root, LV_OBJ_FLAG_HIDDEN));
    CHECK(fixture.status.last_style == 10u);
    ui_runtime_tick(&fixture.runtime, 2u);
    CHECK(all_callbacks_valid(&fixture, 2u));
    lv_test_reset();
    return true;
}

static bool test_started_observer_cannot_reenter_navigation(void)
{
    fixture_t fixture;

    fixture_prepare(&fixture, 2u, 2u, false);
    fixture.observer.navigate_in_started = true;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, NULL).code ==
          UI_OK_TRANSITIONING);
    CHECK(fixture.observer.reentrant_result.code == UI_ERR_REENTRANT);
    CHECK(fixture.observer.reentrant_result.txn_id == 0u);
    ui_runtime_tick(&fixture.runtime, 1u);
    lv_test_reset();
    return true;
}

static bool test_illegal_slot_delete_enters_fail_stop(void)
{
    fixture_t fixture;
    ui_entry_handle_t top;
    uint16_t event_count;

    fixture_prepare(&fixture, 2u, 2u, false);
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(ui_entry_get_top(&fixture.runtime, &top).code == UI_OK);
    CHECK(fixture.pages[0].last_slot_root != NULL);
    event_count = fixture.observer.count;

    lv_obj_del(fixture.pages[0].last_slot_root);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_FAULTED);
    CHECK(!ui_entry_is_valid(&fixture.runtime, top));
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, NULL).code ==
          UI_ERR_FAULTED);
    CHECK(fixture.pages[0].unmount_count[UI_UNMOUNT_REMOVED] == 0u);
    CHECK(fixture.pages[0].destroy_count == 0u);

    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(fixture.observer.count == (uint16_t)(event_count + 1u));
    CHECK(fixture.observer.events[event_count].kind == UI_RUNTIME_FAULTED);
    ui_runtime_tick(&fixture.runtime, 2u);
    CHECK(fixture.observer.count == (uint16_t)(event_count + 1u));
    lv_test_reset();
    return true;
}

static bool test_illegal_slot_delete_during_mount_faults(void)
{
    fixture_t fixture;
    ui_result_t result;

    fixture_prepare(&fixture, 2u, 2u, false);
    fixture.pages[1].delete_slot_during_mount = true;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);

    result = ui_nav_push(&fixture.runtime, 2u, NULL, NULL);
    CHECK(result.code == UI_ERR_FAULTED && result.txn_id != 0u);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_FAULTED);
    CHECK(fixture.pages[1].mount_count == 1u);
    CHECK(fixture.pages[1].unmount_count[UI_UNMOUNT_REMOVED] == 0u);
    CHECK(fixture.pages[1].destroy_count == 0u);
    CHECK(fixture.observer.count == 1u);
    CHECK(fixture.observer.events[0].kind == UI_NAV_STARTED);

    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(fixture.observer.count == 2u);
    CHECK(fixture.observer.events[1].kind == UI_RUNTIME_FAULTED);
    lv_test_reset();
    return true;
}

static bool test_reparented_slot_is_detected_at_safe_tick(void)
{
    fixture_t fixture;
    lv_obj_t *foreign_screen;
    uint16_t event_count;

    fixture_prepare(&fixture, 1u, 1u, false);
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    foreign_screen = lv_obj_create(NULL);
    CHECK(foreign_screen != NULL);
    event_count = fixture.observer.count;

    lv_obj_set_parent(fixture.pages[0].last_slot_root, foreign_screen);
    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_FAULTED);
    CHECK(fixture.observer.count == (uint16_t)(event_count + 1u));
    CHECK(fixture.observer.events[event_count].kind == UI_RUNTIME_FAULTED);
    CHECK(fixture.pages[0].unmount_count[UI_UNMOUNT_REMOVED] == 0u);
    CHECK(fixture.pages[0].destroy_count == 0u);
    lv_test_reset();
    return true;
}

static bool test_external_screen_load_is_detected_at_safe_tick(void)
{
    fixture_t fixture;
    lv_obj_t *foreign_screen;
    uint16_t event_count;

    fixture_prepare(&fixture, 1u, 1u, false);
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    foreign_screen = lv_obj_create(NULL);
    CHECK(foreign_screen != NULL);
    event_count = fixture.observer.count;
    lv_disp_load_scr(foreign_screen);

    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_FAULTED);
    CHECK(fixture.observer.count == (uint16_t)(event_count + 1u));
    CHECK(fixture.observer.events[event_count].kind == UI_RUNTIME_FAULTED);
    CHECK(fixture.pages[0].unmount_count[UI_UNMOUNT_REMOVED] == 0u);
    lv_test_reset();
    return true;
}

static bool test_deleted_initial_focus_does_not_leave_raw_pointer(void)
{
    fixture_t fixture;
    lv_obj_t *focus_object;

    fixture_prepare(&fixture, 2u, 2u, false);
    fixture.pages[0].register_focus = true;
    fixture.pages[1].register_focus = true;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, NULL).code ==
          UI_OK_TRANSITIONING);
    focus_object = fixture.pages[1].last_focus_object;
    CHECK(focus_object != NULL);
    lv_obj_del(focus_object);

    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_RUNNING);
    CHECK(lv_test_indev_group(fixture.keypad) != NULL);
    CHECK(fixture.observer.events[1].kind == UI_NAV_COMPLETED);
    CHECK(all_callbacks_valid(&fixture, 2u));
    lv_test_reset();
    return true;
}

static bool test_illegal_delete_during_unmount_stops_cleanup(void)
{
    fixture_t fixture;
    ui_entry_handle_t second;
    uint16_t event_count;

    fixture_prepare(&fixture, 2u, 2u, false);
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, &second).code ==
          UI_OK_TRANSITIONING);
    ui_runtime_tick(&fixture.runtime, 1u);
    fixture.pages[1].delete_slot_during_unmount = true;
    CHECK(ui_nav_pop(&fixture.runtime).code == UI_OK_TRANSITIONING);
    CHECK(!ui_entry_is_valid(&fixture.runtime, second));
    event_count = fixture.observer.count;

    ui_runtime_tick(&fixture.runtime, 2u);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_FAULTED);
    CHECK(fixture.pages[1].unmount_count[UI_UNMOUNT_REMOVED] == 1u);
    CHECK(fixture.pages[1].destroy_count == 0u);
    CHECK(fixture.observer.count == (uint16_t)(event_count + 1u));
    CHECK(fixture.observer.events[event_count].kind == UI_RUNTIME_FAULTED);
    lv_test_reset();
    return true;
}

static void page_child_delete(lv_event_t *event)
{
    page_state_t *state =
        (page_state_t *)lv_event_get_user_data(event);
    page_probe_t *probe = state->owner;

    if(lv_event_get_code(event) != LV_EVENT_DELETE) return;
    state->view_deleted = true;
    if(probe->navigate_on_child_delete) {
        probe->child_delete_nav_result = ui_nav_pop(probe->runtime);
    }
    if(probe->delete_other_slot_on_child_delete &&
       probe->other_slot_root != NULL) {
        lv_obj_t *other = probe->other_slot_root;
        probe->other_slot_root = NULL;
        lv_obj_del(other);
    }
}

static bool test_cross_slot_delete_during_normal_cleanup_faults(void)
{
    fixture_t fixture;
    uint16_t event_count;

    fixture_prepare(&fixture, 2u, 2u, false);
    fixture.pages[0].delete_other_slot_on_child_delete = true;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, NULL).code ==
          UI_OK_TRANSITIONING);
    fixture.pages[0].other_slot_root = fixture.pages[1].last_slot_root;
    event_count = fixture.observer.count;

    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_FAULTED);
    CHECK(fixture.pages[0].unmount_count[UI_UNMOUNT_REBUILD] == 1u);
    CHECK(fixture.observer.count == (uint16_t)(event_count + 1u));
    if(fixture.observer.events[event_count].kind != UI_RUNTIME_FAULTED) {
        fprintf(stderr, "cross-slot terminal kind=%d state=%d count=%u\n",
                (int)fixture.observer.events[event_count].kind,
                (int)ui_runtime_get_state(&fixture.runtime),
                (unsigned)fixture.observer.count);
    }
    CHECK(fixture.observer.events[event_count].kind == UI_RUNTIME_FAULTED);
    lv_test_reset();
    return true;
}

static bool test_child_delete_event_cannot_reenter_runtime_tick(void)
{
    fixture_t fixture;

    fixture_prepare(&fixture, 2u, 2u, false);
    fixture.pages[0].navigate_on_child_delete = true;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, NULL).code ==
          UI_OK_TRANSITIONING);

    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(fixture.pages[0].child_delete_nav_result.code ==
          UI_ERR_REENTRANT);
    CHECK(fixture.pages[0].child_delete_nav_result.txn_id == 0u);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_RUNNING);
    CHECK(fixture.observer.events[1].kind == UI_NAV_COMPLETED);
    CHECK(all_callbacks_valid(&fixture, 2u));
    lv_test_reset();
    return true;
}

static uint32_t random_next(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static bool test_deterministic_random_state_machine(void)
{
    fixture_t fixture;
    ui_entry_handle_t model[8];
    ui_entry_handle_t output;
    ui_entry_handle_t actual;
    ui_entry_handle_t removed;
    ui_result_t result;
    uint16_t depth = 1u;
    uint32_t random_state = UINT32_C(0x13579BDF);
    uint32_t step;
    uint16_t route;
    uint16_t position;

    memset(&removed, 0, sizeof(removed));
    fixture_prepare(&fixture, 4u, 8u, false);
    fixture.routes[0].view_policy = UI_VIEW_RETAIN;
    fixture.routes[2].view_policy = UI_VIEW_RETAIN;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(ui_entry_get_top(&fixture.runtime, &model[0]).code == UI_OK);

    for(step = 0; step < 100000u; ++step) {
        uint32_t choice = random_next(&random_state) % 8u;
        route = (uint16_t)(random_next(&random_state) % 4u + 1u);
        memset(&output, 0, sizeof(output));
        memset(&removed, 0, sizeof(removed));

        switch(choice) {
            case 0u:
            case 1u:
                result = ui_nav_push(
                    &fixture.runtime, route, NULL, &output);
                if(depth == 8u) {
                    CHECK(result.code == UI_ERR_BACK_STACK_FULL);
                }
                else {
                    CHECK(result.code == UI_OK_TRANSITIONING);
                    model[depth++] = output;
                }
                break;
            case 2u:
                removed = model[depth - 1u];
                result = ui_nav_pop(&fixture.runtime);
                if(depth == 1u) {
                    CHECK(result.code == UI_OK_NO_CHANGE);
                    memset(&removed, 0, sizeof(removed));
                }
                else {
                    CHECK(result.code == UI_OK_TRANSITIONING);
                    --depth;
                }
                break;
            case 3u:
                removed = model[depth - 1u];
                result = ui_nav_replace(
                    &fixture.runtime, route, NULL, &output);
                CHECK(result.code == UI_OK_TRANSITIONING);
                model[depth - 1u] = output;
                break;
            case 4u:
                removed = model[depth - 1u];
                result = ui_nav_reset(
                    &fixture.runtime, route, NULL, &output);
                CHECK(result.code == UI_OK_TRANSITIONING);
                model[0] = output;
                depth = 1u;
                break;
            case 5u:
                position = (uint16_t)(
                    random_next(&random_state) % depth);
                removed = model[depth - 1u];
                result = ui_nav_pop_to(
                    &fixture.runtime, model[position]);
                if(position == depth - 1u) {
                    CHECK(result.code == UI_OK_NO_CHANGE);
                    memset(&removed, 0, sizeof(removed));
                }
                else {
                    CHECK(result.code == UI_OK_TRANSITIONING);
                    depth = (uint16_t)(position + 1u);
                }
                break;
            case 6u:
                position = (uint16_t)(
                    random_next(&random_state) % depth);
                CHECK(ui_entry_invalidate(
                    &fixture.runtime, model[position]).code == UI_OK);
                CHECK(ui_runtime_reclaim_views(
                    &fixture.runtime,
                    (random_next(&random_state) & 1u) != 0u
                        ? UI_RECLAIM_ONE_OLDEST
                        : UI_RECLAIM_ALL_HIDDEN).code == UI_OK);
                break;
            default:
                CHECK(ui_runtime_suspend(&fixture.runtime).code ==
                      UI_OK_TRANSITIONING);
                ui_runtime_tick(&fixture.runtime, step);
                CHECK(ui_runtime_get_state(&fixture.runtime) ==
                      UI_RUNTIME_STATE_SUSPENDED);
                CHECK(ui_entry_invalidate(
                    &fixture.runtime, model[depth - 1u]).code == UI_OK);
                CHECK(ui_runtime_resume(&fixture.runtime).code ==
                      UI_OK_TRANSITIONING);
                break;
        }

        ui_runtime_tick(&fixture.runtime, step);
        CHECK(ui_runtime_get_state(&fixture.runtime) ==
              UI_RUNTIME_STATE_RUNNING);
        CHECK(ui_entry_get_top(&fixture.runtime, &actual).code == UI_OK);
        CHECK(actual.slot == model[depth - 1u].slot);
        CHECK(actual.entry_generation ==
              model[depth - 1u].entry_generation);
        for(position = 0; position < depth; ++position) {
            CHECK(ui_entry_is_valid(&fixture.runtime, model[position]));
        }
        if(removed.runtime_generation != 0u) {
            CHECK(!ui_entry_is_valid(&fixture.runtime, removed));
        }
        CHECK(lv_test_object_count() < 40u);
        CHECK(all_callbacks_valid(&fixture, 4u));
    }
    lv_test_reset();
    return true;
}

static bool test_init_mount_failure_preserves_bootstrap(void)
{
    fixture_t fixture;
    lv_obj_t *bootstrap;
    ui_result_t result;

    fixture_prepare(&fixture, 1u, 1u, false);
    fixture.pages[0].fail_mount = true;
    bootstrap = lv_obj_create(NULL);
    CHECK(bootstrap != NULL);
    lv_disp_load_scr(bootstrap);

    result = ui_runtime_init(&fixture.runtime, &fixture.config);
    CHECK(result.code == UI_ERR_VIEW_MOUNT);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_UNINITIALIZED);
    CHECK(lv_test_display_active_screen(fixture.display) == bootstrap);
    CHECK(lv_test_object_count() == 1u);
    CHECK(fixture.pages[0].mount_count == 1u);
    CHECK(fixture.pages[0].destroy_count == 1u);
    CHECK(fixture.pages[0].unmount_count[UI_UNMOUNT_SHUTDOWN] == 0u);
    CHECK(fixture.observer.count == 0u);
    lv_test_reset();
    return true;
}

static bool test_status_mount_failure_rolls_back_page(void)
{
    fixture_t fixture;
    lv_obj_t *bootstrap;
    ui_result_t result;

    fixture_prepare(&fixture, 1u, 1u, true);
    fixture.status.fail_mount = true;
    bootstrap = lv_obj_create(NULL);
    CHECK(bootstrap != NULL);
    lv_disp_load_scr(bootstrap);

    result = ui_runtime_init(&fixture.runtime, &fixture.config);
    CHECK(result.code == UI_ERR_VIEW_MOUNT);
    CHECK(lv_test_display_active_screen(fixture.display) == bootstrap);
    CHECK(lv_test_object_count() == 1u);
    CHECK(fixture.status.mount_count == 1u);
    CHECK(fixture.status.unmount_count == 0u);
    CHECK(fixture.pages[0].unmount_count[UI_UNMOUNT_SHUTDOWN] == 1u);
    CHECK(fixture.pages[0].destroy_count == 1u);
    CHECK(all_callbacks_valid(&fixture, 1u));
    lv_test_reset();
    return true;
}

static bool test_host_creation_failure_rolls_back_and_restores_group(void)
{
    fixture_t fixture;
    lv_obj_t *bootstrap;
    lv_group_t *previous_group;
    ui_result_t result;

    fixture_prepare(&fixture, 1u, 1u, false);
    bootstrap = lv_obj_create(NULL);
    CHECK(bootstrap != NULL);
    lv_disp_load_scr(bootstrap);
    previous_group = lv_group_create();
    CHECK(previous_group != NULL);
    lv_indev_set_group(fixture.keypad, previous_group);
    lv_test_fail_object_create_after(3);

    result = ui_runtime_init(&fixture.runtime, &fixture.config);
    CHECK(result.code == UI_ERR_HOST);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_UNINITIALIZED);
    CHECK(lv_test_display_active_screen(fixture.display) == bootstrap);
    CHECK(lv_test_indev_group(fixture.keypad) == previous_group);
    CHECK(lv_test_object_count() == 1u);
    CHECK(lv_test_group_count() == 1u);
    CHECK(fixture.pages[0].create_count == 0u);
    CHECK(fixture.observer.count == 0u);
    lv_test_reset();
    return true;
}

static bool test_thread_check_and_invalid_configuration(void)
{
    fixture_t fixture;
    ui_entry_handle_t top;
    ui_result_t result;

    fixture_prepare(&fixture, 1u, 1u, false);
    fixture.routes[0].enter_transition.kind = UI_TRANSITION_FADE;
    result = ui_runtime_init(&fixture.runtime, &fixture.config);
    CHECK(result.code == UI_ERR_INVALID_ARGUMENT);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_UNINITIALIZED);

    fixture.routes[0].enter_transition.duration_ms = 1u;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    fixture.thread_ok = false;
    CHECK(ui_entry_get_top(&fixture.runtime, &top).code ==
          UI_ERR_WRONG_THREAD);
    CHECK(ui_nav_pop(&fixture.runtime).code == UI_ERR_WRONG_THREAD);
    fixture.thread_ok = true;
    lv_test_reset();
    return true;
}

static bool test_slide_transition_waits_for_completion(void)
{
    fixture_t fixture;
    lv_obj_t *source_root;
    lv_obj_t *target_root;
    lv_obj_t *root_screen;
    lv_obj_t *overlay_host;
    lv_obj_t *input_shield;
    ui_result_t result;

    fixture_prepare(&fixture, 2u, 2u, false);
    fixture.routes[1].enter_transition.kind = UI_TRANSITION_SLIDE_LEFT;
    fixture.routes[1].enter_transition.duration_ms = 100u;
    fixture.routes[1].enter_transition.delay_ms = 20u;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    source_root = fixture.pages[0].last_slot_root;

    result = ui_nav_push(&fixture.runtime, 2u, NULL, NULL);
    CHECK(result.code == UI_OK_TRANSITIONING && result.txn_id != 0u);
    target_root = fixture.pages[1].last_slot_root;
    root_screen = lv_obj_get_parent(lv_obj_get_parent(
        lv_obj_get_parent(target_root)));
    CHECK(root_screen != NULL);
    overlay_host = lv_obj_get_child(root_screen, 2);
    CHECK(overlay_host != NULL);
    input_shield = lv_obj_get_child(overlay_host, 3);
    CHECK(input_shield != NULL);
    CHECK(lv_test_anim_count() == 1u);
    CHECK(lv_test_object_x(source_root) == 0);
    CHECK(lv_test_object_x(target_root) == 800);
    CHECK(fixture.observer.count == 1u);
    CHECK(!lv_obj_has_flag(input_shield, LV_OBJ_FLAG_HIDDEN));
    CHECK(ui_nav_pop(&fixture.runtime).code == UI_ERR_BUSY);

    lv_test_anim_set_progress(512);
    CHECK(lv_test_object_x(source_root) == -400);
    CHECK(lv_test_object_x(target_root) == 400);
    ui_runtime_tick(&fixture.runtime, 1000000u);
    CHECK(fixture.observer.count == 1u);
    CHECK(!lv_obj_has_flag(input_shield, LV_OBJ_FLAG_HIDDEN));
    CHECK(fixture.pages[0].unmount_count[UI_UNMOUNT_REBUILD] == 0u);

    lv_test_anim_complete_all();
    CHECK(fixture.observer.count == 1u);
    ui_runtime_tick(&fixture.runtime, 1000001u);
    CHECK(fixture.observer.count == 2u);
    CHECK(fixture.observer.events[1].kind == UI_NAV_COMPLETED);
    CHECK(fixture.observer.events[1].txn_id == result.txn_id);
    CHECK(fixture.observer.events[1].detail_flags == UI_EVENT_DETAIL_NONE);
    CHECK(fixture.pages[0].unmount_count[UI_UNMOUNT_REBUILD] == 1u);
    CHECK(lv_test_object_x(target_root) == 0);
    CHECK(lv_test_object_opa(target_root) == LV_OPA_COVER);
    CHECK(lv_obj_has_flag(input_shield, LV_OBJ_FLAG_HIDDEN));
    CHECK(lv_test_anim_count() == 0u);
    CHECK(all_callbacks_valid(&fixture, 2u));
    lv_test_reset();
    return true;
}

static bool test_exit_fade_and_stale_completion_are_isolated(void)
{
    fixture_t fixture;
    lv_obj_t *source_root;
    lv_obj_t *target_root;
    uint16_t event_count;

    fixture_prepare(&fixture, 2u, 2u, false);
    fixture.routes[1].enter_transition.kind = UI_TRANSITION_FADE;
    fixture.routes[1].enter_transition.duration_ms = 80u;
    fixture.routes[1].exit_transition.kind = UI_TRANSITION_FADE;
    fixture.routes[1].exit_transition.duration_ms = 80u;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);

    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, NULL).code ==
          UI_OK_TRANSITIONING);
    lv_test_anim_complete_all();
    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(fixture.observer.events[1].kind == UI_NAV_COMPLETED);

    CHECK(ui_nav_pop(&fixture.runtime).code == UI_OK_TRANSITIONING);
    source_root = fixture.pages[1].last_slot_root;
    target_root = fixture.pages[0].last_slot_root;
    event_count = fixture.observer.count;
    CHECK(lv_test_anim_count() == 1u);
    CHECK(lv_test_object_opa(source_root) == LV_OPA_COVER);
    CHECK(lv_test_object_opa(target_root) == LV_OPA_COVER);

    lv_test_repeat_last_anim_ready();
    ui_runtime_tick(&fixture.runtime, 2u);
    CHECK(fixture.observer.count == event_count);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, NULL).code == UI_ERR_BUSY);
    lv_test_anim_set_progress(512);
    CHECK(lv_test_object_opa(source_root) == 127u);
    CHECK(lv_test_object_opa(target_root) == LV_OPA_COVER);

    lv_test_anim_complete_all();
    lv_test_repeat_last_anim_ready();
    ui_runtime_tick(&fixture.runtime, 3u);
    CHECK(fixture.observer.count == (uint16_t)(event_count + 1u));
    CHECK(fixture.observer.events[event_count].kind == UI_NAV_COMPLETED);
    CHECK(lv_test_object_opa(target_root) == LV_OPA_COVER);
    CHECK(lv_test_anim_count() == 0u);

    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, NULL).code ==
          UI_OK_TRANSITIONING);
    event_count = fixture.observer.count;
    CHECK(lv_test_anim_count() == 1u);
    lv_test_repeat_anim_ready(1u);
    ui_runtime_tick(&fixture.runtime, 4u);
    CHECK(fixture.observer.count == event_count);
    CHECK(ui_nav_pop(&fixture.runtime).code == UI_ERR_BUSY);
    lv_test_anim_complete_all();
    ui_runtime_tick(&fixture.runtime, 5u);
    CHECK(fixture.observer.count == (uint16_t)(event_count + 1u));
    CHECK(fixture.observer.events[event_count].kind == UI_NAV_COMPLETED);
    CHECK(all_callbacks_valid(&fixture, 2u));
    lv_test_reset();
    return true;
}

static bool test_transition_start_failure_snaps_degraded(void)
{
    fixture_t fixture;
    ui_result_t result;

    fixture_prepare(&fixture, 2u, 2u, false);
    fixture.routes[1].enter_transition.kind = UI_TRANSITION_FADE;
    fixture.routes[1].enter_transition.duration_ms = 100u;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    lv_test_fail_next_anim_start();

    result = ui_nav_push(&fixture.runtime, 2u, NULL, NULL);
    CHECK(result.code == UI_OK_TRANSITIONING);
    CHECK(lv_test_anim_count() == 0u);
    CHECK(fixture.observer.count == 1u);
    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(fixture.observer.count == 2u);
    CHECK(fixture.observer.events[1].kind == UI_NAV_COMPLETED_DEGRADED);
    CHECK(fixture.observer.events[1].code == UI_OK);
    CHECK(fixture.observer.events[1].detail_flags ==
          UI_EVENT_DETAIL_HOST_SNAP);
    CHECK(fixture.pages[0].unmount_count[UI_UNMOUNT_REBUILD] == 1u);
    CHECK(all_callbacks_valid(&fixture, 2u));
    lv_test_reset();
    return true;
}

static bool test_transition_watchdog_wraparound(void)
{
    fixture_t fixture;
    uint32_t start_ms = UINT32_MAX - 100u;

    fixture_prepare(&fixture, 2u, 2u, false);
    fixture.routes[1].enter_transition.kind = UI_TRANSITION_SLIDE_UP;
    fixture.routes[1].enter_transition.duration_ms = 100u;
    fixture.routes[1].enter_transition.delay_ms = 20u;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    ui_runtime_tick(&fixture.runtime, start_ms);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, NULL).code ==
          UI_OK_TRANSITIONING);
    CHECK(lv_test_anim_count() == 1u);

    ui_runtime_tick(&fixture.runtime, 268u);
    CHECK(fixture.observer.count == 1u);
    CHECK(lv_test_anim_count() == 1u);
    ui_runtime_tick(&fixture.runtime, 269u);
    CHECK(fixture.observer.count == 2u);
    CHECK(fixture.observer.events[1].kind == UI_NAV_COMPLETED_DEGRADED);
    CHECK(fixture.observer.events[1].detail_flags ==
          (UI_EVENT_DETAIL_TRANSITION_TIMEOUT |
           UI_EVENT_DETAIL_HOST_SNAP));
    CHECK(lv_test_anim_count() == 0u);
    CHECK(all_callbacks_valid(&fixture, 2u));
    lv_test_reset();
    return true;
}

static bool test_suspend_snaps_transition_without_degradation(void)
{
    fixture_t fixture;

    fixture_prepare(&fixture, 2u, 2u, false);
    fixture.routes[1].enter_transition.kind = UI_TRANSITION_SLIDE_DOWN;
    fixture.routes[1].enter_transition.duration_ms = 100u;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, NULL).code ==
          UI_OK_TRANSITIONING);
    CHECK(ui_runtime_suspend(&fixture.runtime).code == UI_OK_TRANSITIONING);

    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_SUSPENDED);
    CHECK(lv_test_anim_count() == 0u);
    CHECK(fixture.observer.count == 3u);
    CHECK(fixture.observer.events[1].kind == UI_NAV_COMPLETED);
    CHECK(fixture.observer.events[1].detail_flags == UI_EVENT_DETAIL_NONE);
    CHECK(fixture.observer.events[2].kind == UI_RUNTIME_SUSPENDED);
    CHECK(ui_runtime_resume(&fixture.runtime).code == UI_OK_TRANSITIONING);
    ui_runtime_tick(&fixture.runtime, 2u);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_RUNNING);
    CHECK(all_callbacks_valid(&fixture, 2u));
    lv_test_reset();
    return true;
}

static bool test_modal_back_and_navigation_blocking(void)
{
    fixture_t fixture;
    ui_overlay_request_t request;
    ui_overlay_handle_t modal;
    overlay_state_t *state;
    uint32_t page_back_count;

    fixture_prepare(&fixture, 2u, 2u, false);
    fixture_enable_overlays(&fixture);
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    request = overlay_request(1u, 10, 1, 100);
    CHECK(ui_overlay_show(&fixture.runtime, 101u, &request, &modal).code ==
          UI_OK);
    CHECK(ui_overlay_is_valid(&fixture.runtime, modal));
    state = find_overlay_state(&fixture.overlay_probes[0], 1);
    CHECK(state != NULL && state->mount_count == 1u);
    CHECK(state->activate_count[UI_OVERLAY_ACTIVATE_NEW] == 1u);
    CHECK(state->render_count == 1u);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, NULL).code ==
          UI_ERR_MODAL_ACTIVE);

    page_back_count = fixture.pages[0].back_count;
    CHECK(ui_nav_back(&fixture.runtime).code == UI_OK_NO_CHANGE);
    CHECK(fixture.pages[0].back_count == page_back_count);
    CHECK(!ui_overlay_is_valid(&fixture.runtime, modal));
    CHECK(ui_overlay_dismiss(&fixture.runtime, modal).code ==
          UI_ERR_INVALID_OVERLAY);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, NULL).code == UI_ERR_BUSY);
    CHECK(ui_nav_back(&fixture.runtime).code == UI_OK_NO_CHANGE);
    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(state->unmount_count[UI_OVERLAY_UNMOUNT_DISMISSED] == 1u);
    CHECK(state->dismiss_count == 1u);
    CHECK(state->dismiss_reason == UI_OVERLAY_DISMISS_EXPLICIT);
    CHECK(fixture.overlay_probes[0].destroy_count == 1u);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, NULL).code ==
          UI_OK_TRANSITIONING);
    ui_runtime_tick(&fixture.runtime, 2u);
    CHECK(!fixture.overlay_probes[0].callback_error);
    lv_test_reset();
    return true;
}

static bool test_notice_priority_preemption_and_fifo_resume(void)
{
    fixture_t fixture;
    ui_overlay_request_t request;
    ui_overlay_handle_t low;
    ui_overlay_handle_t equal;
    ui_overlay_handle_t high;
    overlay_state_t *low_state;
    overlay_state_t *equal_state;
    overlay_state_t *high_state;

    fixture_prepare(&fixture, 1u, 1u, false);
    fixture_enable_overlays(&fixture);
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);

    request = overlay_request(1u, 1, 1, 10);
    CHECK(ui_overlay_show(&fixture.runtime, 102u, &request, &low).code ==
          UI_OK);
    request = overlay_request(2u, 1, 2, 20);
    CHECK(ui_overlay_show(&fixture.runtime, 102u, &request, &equal).code ==
          UI_OK);
    equal_state = find_overlay_state(&fixture.overlay_probes[1], 2);
    CHECK(equal_state != NULL && equal_state->mount_count == 0u);

    request = overlay_request(3u, 5, 3, 30);
    CHECK(ui_overlay_show(&fixture.runtime, 102u, &request, &high).code ==
          UI_OK);
    low_state = find_overlay_state(&fixture.overlay_probes[1], 1);
    high_state = find_overlay_state(&fixture.overlay_probes[1], 3);
    CHECK(low_state != NULL && high_state != NULL);
    CHECK(low_state->deactivate_count[
              UI_OVERLAY_DEACTIVATE_PREEMPTED] == 1u);
    CHECK(low_state->dismiss_count == 0u);
    CHECK(high_state->activate_count[UI_OVERLAY_ACTIVATE_PREEMPTED] == 1u);
    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(low_state->unmount_count[UI_OVERLAY_UNMOUNT_PREEMPTED] == 1u);
    CHECK(low_state->dismiss_count == 0u);
    CHECK(ui_overlay_is_valid(&fixture.runtime, low));

    CHECK(ui_overlay_dismiss(&fixture.runtime, high).code == UI_OK);
    ui_runtime_tick(&fixture.runtime, 2u);
    CHECK(high_state->dismiss_count == 1u);
    CHECK(low_state->mount_count == 2u);
    CHECK(low_state->activate_count[UI_OVERLAY_ACTIVATE_RESUME] == 1u);
    CHECK(equal_state->mount_count == 0u);

    CHECK(ui_overlay_dismiss(&fixture.runtime, low).code == UI_OK);
    ui_runtime_tick(&fixture.runtime, 3u);
    CHECK(equal_state->mount_count == 1u);
    CHECK(equal_state->activate_count[UI_OVERLAY_ACTIVATE_NEW] == 1u);
    CHECK(ui_overlay_is_valid(&fixture.runtime, equal));
    CHECK(!fixture.overlay_probes[1].callback_error);
    lv_test_reset();
    return true;
}

static bool test_transient_latest_wins_and_timeout(void)
{
    fixture_t fixture;
    ui_overlay_request_t first_request;
    ui_overlay_request_t second_request;
    ui_overlay_handle_t first;
    ui_overlay_handle_t second;
    overlay_state_t *first_state;
    overlay_state_t *second_state;

    fixture_prepare(&fixture, 1u, 1u, false);
    fixture_enable_overlays(&fixture);
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    ui_runtime_tick(&fixture.runtime, 1000u);
    first_request = overlay_request(1u, 0, 1, 10);
    first_request.timeout_ms = 50u;
    CHECK(ui_overlay_show(
              &fixture.runtime, 103u, &first_request, &first).code == UI_OK);
    first_state = find_overlay_state(&fixture.overlay_probes[2], 1);
    CHECK(first_state != NULL && first_state->mount_count == 1u);

    second_request = overlay_request(2u, 0, 2, 20);
    second_request.timeout_ms = 100u;
    CHECK(ui_overlay_show(
              &fixture.runtime, 103u, &second_request, &second).code == UI_OK);
    second_state = find_overlay_state(&fixture.overlay_probes[2], 2);
    CHECK(second_state != NULL && second_state->mount_count == 1u);
    CHECK(!ui_overlay_is_valid(&fixture.runtime, first));
    CHECK(ui_overlay_is_valid(&fixture.runtime, second));
    CHECK(first_state->deactivate_count[
              UI_OVERLAY_DEACTIVATE_PREEMPTED] == 1u);
    ui_runtime_tick(&fixture.runtime, 1001u);
    CHECK(first_state->dismiss_reason == UI_OVERLAY_DISMISS_REPLACED);
    CHECK(first_state->dismiss_count == 1u);
    ui_runtime_tick(&fixture.runtime, 1099u);
    CHECK(ui_overlay_is_valid(&fixture.runtime, second));
    ui_runtime_tick(&fixture.runtime, 1100u);
    CHECK(!ui_overlay_is_valid(&fixture.runtime, second));
    ui_runtime_tick(&fixture.runtime, 1101u);
    CHECK(second_state->dismiss_count == 1u);
    CHECK(second_state->dismiss_reason == UI_OVERLAY_DISMISS_TIMEOUT);
    CHECK(!fixture.overlay_probes[2].callback_error);
    lv_test_reset();
    return true;
}

static bool test_transient_timeout_wraparound(void)
{
    fixture_t fixture;
    ui_overlay_request_t request;
    ui_overlay_handle_t transient;

    fixture_prepare(&fixture, 1u, 1u, false);
    fixture_enable_overlays(&fixture);
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    ui_runtime_tick(&fixture.runtime, UINT32_MAX - 5u);
    request = overlay_request(1u, 0, 1, 10);
    request.timeout_ms = 10u;
    CHECK(ui_overlay_show(
              &fixture.runtime, 103u, &request, &transient).code == UI_OK);
    ui_runtime_tick(&fixture.runtime, UINT32_MAX);
    CHECK(ui_overlay_is_valid(&fixture.runtime, transient));
    ui_runtime_tick(&fixture.runtime, 3u);
    CHECK(ui_overlay_is_valid(&fixture.runtime, transient));
    ui_runtime_tick(&fixture.runtime, 4u);
    CHECK(!ui_overlay_is_valid(&fixture.runtime, transient));
    ui_runtime_tick(&fixture.runtime, 5u);
    CHECK(!fixture.overlay_probes[2].callback_error);
    lv_test_reset();
    return true;
}

static bool test_overlay_same_key_update_and_immutable_fields(void)
{
    fixture_t fixture;
    ui_overlay_request_t request;
    ui_overlay_handle_t first;
    ui_overlay_handle_t updated;
    overlay_state_t *state;
    uint32_t objects;

    fixture_prepare(&fixture, 1u, 1u, false);
    fixture_enable_overlays(&fixture);
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    request = overlay_request(7u, 2, 7, 10);
    CHECK(ui_overlay_show(&fixture.runtime, 102u, &request, &first).code ==
          UI_OK);
    state = find_overlay_state(&fixture.overlay_probes[1], 7);
    CHECK(state != NULL);
    objects = (uint32_t)lv_test_object_count();

    request = overlay_request(7u, 2, 7, 99);
    CHECK(ui_overlay_show(&fixture.runtime, 102u, &request, &updated).code ==
          UI_OK);
    CHECK(updated.slot == first.slot &&
          updated.overlay_generation == first.overlay_generation);
    CHECK(state->value == 99 && state->update_count == 1u);
    CHECK(state->mount_count == 1u);
    CHECK(lv_test_object_count() == objects);
    request.priority = 3;
    CHECK(ui_overlay_show(&fixture.runtime, 102u, &request, NULL).code ==
          UI_ERR_INVALID_ARGUMENT);
    CHECK(state->value == 99 && state->update_count == 1u);
    CHECK(!fixture.overlay_probes[1].callback_error);
    lv_test_reset();
    return true;
}

static bool test_entry_owned_overlay_closes_on_navigation(void)
{
    fixture_t fixture;
    ui_overlay_request_t request;
    ui_overlay_handle_t notice;
    ui_entry_handle_t owner;
    overlay_state_t *state;

    fixture_prepare(&fixture, 2u, 2u, false);
    fixture_enable_overlays(&fixture);
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(ui_entry_get_top(&fixture.runtime, &owner).code == UI_OK);
    request = overlay_request(1u, 1, 1, 10);
    request.owner_kind = UI_OVERLAY_OWNER_ENTRY;
    request.owner = owner;
    CHECK(ui_overlay_show(&fixture.runtime, 102u, &request, &notice).code ==
          UI_OK);
    state = find_overlay_state(&fixture.overlay_probes[1], 1);
    CHECK(ui_nav_push(&fixture.runtime, 2u, NULL, NULL).code ==
          UI_OK_TRANSITIONING);
    CHECK(!ui_overlay_is_valid(&fixture.runtime, notice));
    CHECK(state->deactivate_count[
              UI_OVERLAY_DEACTIVATE_DISMISSED] == 1u);
    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(state->dismiss_reason ==
          UI_OVERLAY_DISMISS_OWNER_DEACTIVATED);
    CHECK(state->dismiss_count == 1u);
    CHECK(!fixture.overlay_probes[1].callback_error);
    lv_test_reset();
    return true;
}

static bool test_overlay_suspend_resume_and_input_rules(void)
{
    fixture_t fixture;
    ui_overlay_request_t modal_request;
    ui_overlay_request_t notice_request;
    ui_overlay_request_t transient_request;
    ui_overlay_handle_t modal;
    ui_overlay_handle_t notice;
    ui_overlay_handle_t transient;
    overlay_state_t *modal_state;
    overlay_state_t *notice_state;
    overlay_state_t *transient_state;
    lv_obj_t *overlay_host;
    lv_obj_t *input_shield;

    fixture_prepare(&fixture, 1u, 1u, false);
    fixture_enable_overlays(&fixture);
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    modal_request = overlay_request(1u, 1, 1, 10);
    notice_request = overlay_request(1u, 1, 3, 30);
    transient_request = overlay_request(1u, 0, 2, 20);
    CHECK(ui_overlay_show(
              &fixture.runtime, 102u, &notice_request, &notice).code == UI_OK);
    CHECK(ui_overlay_show(
              &fixture.runtime, 101u, &modal_request, &modal).code == UI_OK);
    CHECK(ui_overlay_show(
              &fixture.runtime, 103u, &transient_request,
              &transient).code == UI_OK);
    modal_state = find_overlay_state(&fixture.overlay_probes[0], 1);
    notice_state = find_overlay_state(&fixture.overlay_probes[1], 3);
    transient_state = find_overlay_state(&fixture.overlay_probes[2], 2);
    CHECK(modal_state != NULL && notice_state != NULL &&
          transient_state != NULL);
    CHECK(lv_obj_has_flag(
        modal_state->interactive_child, LV_OBJ_FLAG_CLICKABLE));
    CHECK(lv_obj_has_flag(modal_state->root, LV_OBJ_FLAG_CLICKABLE));
    CHECK(!lv_obj_has_flag(
        transient_state->interactive_child, LV_OBJ_FLAG_CLICKABLE));
    CHECK(lv_test_child_index(
              lv_obj_get_parent(lv_obj_get_parent(notice_state->root)),
              lv_obj_get_parent(notice_state->root)) == 0);
    CHECK(lv_test_child_index(
              lv_obj_get_parent(lv_obj_get_parent(modal_state->root)),
              lv_obj_get_parent(modal_state->root)) == 1);
    CHECK(lv_test_child_index(
              lv_obj_get_parent(lv_obj_get_parent(transient_state->root)),
              lv_obj_get_parent(transient_state->root)) == 2);
    overlay_host = lv_obj_get_parent(lv_obj_get_parent(notice_state->root));
    input_shield = lv_obj_get_child(overlay_host, 3);
    CHECK(input_shield != NULL);
    CHECK(lv_obj_has_flag(input_shield, LV_OBJ_FLAG_HIDDEN));

    CHECK(ui_runtime_suspend(&fixture.runtime).code == UI_OK_TRANSITIONING);
    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(modal_state->deactivate_count[
              UI_OVERLAY_DEACTIVATE_DISPLAY_SUSPEND] == 1u);
    CHECK(transient_state->deactivate_count[
              UI_OVERLAY_DEACTIVATE_DISPLAY_SUSPEND] == 1u);
    CHECK(notice_state->deactivate_count[
              UI_OVERLAY_DEACTIVATE_DISPLAY_SUSPEND] == 1u);
    CHECK(ui_runtime_resume(&fixture.runtime).code == UI_OK_TRANSITIONING);
    ui_runtime_tick(&fixture.runtime, 2u);
    CHECK(modal_state->activate_count[
              UI_OVERLAY_ACTIVATE_DISPLAY_RESUME] == 1u);
    CHECK(transient_state->activate_count[
              UI_OVERLAY_ACTIVATE_DISPLAY_RESUME] == 1u);
    CHECK(notice_state->activate_count[
              UI_OVERLAY_ACTIVATE_DISPLAY_RESUME] == 1u);
    CHECK(ui_overlay_is_valid(&fixture.runtime, modal));
    CHECK(ui_overlay_is_valid(&fixture.runtime, transient));
    CHECK(ui_overlay_is_valid(&fixture.runtime, notice));
    CHECK(!fixture.overlay_probes[0].callback_error);
    CHECK(!fixture.overlay_probes[2].callback_error);
    CHECK(!fixture.overlay_probes[1].callback_error);
    lv_test_reset();
    return true;
}

static bool test_overlay_mount_failure_capacity_and_consume_back(void)
{
    fixture_t fixture;
    ui_overlay_request_t request;
    ui_overlay_handle_t first;
    ui_overlay_handle_t failed;
    ui_overlay_handle_t waiting;
    overlay_state_t *first_state;
    overlay_state_t *failed_state;
    overlay_state_t *waiting_state;

    fixture_prepare(&fixture, 1u, 1u, false);
    fixture_enable_overlays(&fixture);
    fixture.overlay_descs[0].modal_back_policy = UI_MODAL_BACK_CONSUME;
    fixture.overlay_probes[0].fail_mount_id = 2;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);

    request = overlay_request(1u, 1, 1, 10);
    CHECK(ui_overlay_show(&fixture.runtime, 101u, &request, &first).code ==
          UI_OK);
    first_state = find_overlay_state(&fixture.overlay_probes[0], 1);
    CHECK(first_state != NULL);
    request = overlay_request(2u, 5, 2, 20);
    memset(&failed, 0xA5, sizeof(failed));
    CHECK(ui_overlay_show(&fixture.runtime, 101u, &request, &failed).code ==
          UI_ERR_VIEW_MOUNT);
    CHECK(failed.runtime_generation == 0u);
    failed_state = find_overlay_state(&fixture.overlay_probes[0], 2);
    CHECK(failed_state != NULL && failed_state->mount_count == 1u);
    CHECK(ui_overlay_is_valid(&fixture.runtime, first));
    CHECK(ui_nav_back(&fixture.runtime).code == UI_OK_NO_CHANGE);
    CHECK(ui_overlay_is_valid(&fixture.runtime, first));

    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(failed_state->dismiss_reason ==
          UI_OVERLAY_DISMISS_ACTIVATION_FAILED);
    CHECK(failed_state->dismiss_count == 1u);
    request = overlay_request(3u, 0, 3, 30);
    CHECK(ui_overlay_show(
              &fixture.runtime, 101u, &request, &waiting).code == UI_OK);
    waiting_state = find_overlay_state(&fixture.overlay_probes[0], 3);
    CHECK(waiting_state != NULL && waiting_state->mount_count == 0u);
    request = overlay_request(4u, -1, 4, 40);
    CHECK(ui_overlay_show(&fixture.runtime, 101u, &request, NULL).code ==
          UI_ERR_CAPACITY_FULL);

    CHECK(ui_overlay_dismiss(&fixture.runtime, first).code == UI_OK);
    ui_runtime_tick(&fixture.runtime, 2u);
    CHECK(waiting_state->mount_count == 1u);
    CHECK(waiting_state->activate_count[UI_OVERLAY_ACTIVATE_NEW] == 1u);
    CHECK(ui_overlay_is_valid(&fixture.runtime, waiting));
    CHECK(!fixture.overlay_probes[0].callback_error);
    lv_test_reset();
    return true;
}

static bool test_overlay_request_validation_and_notice_input(void)
{
    fixture_t fixture;
    ui_overlay_request_t request;
    ui_overlay_handle_t notice;
    ui_entry_handle_t owner;
    overlay_state_t *state;

    fixture_prepare(&fixture, 1u, 1u, false);
    fixture_enable_overlays(&fixture);
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    CHECK(ui_entry_get_top(&fixture.runtime, &owner).code == UI_OK);

    request = overlay_request(1u, 1, 1, 10);
    request.owner_kind = UI_OVERLAY_OWNER_ENTRY;
    request.owner = owner;
    ++request.owner.entry_generation;
    CHECK(ui_overlay_show(&fixture.runtime, 102u, &request, NULL).code ==
          UI_ERR_INVALID_ENTRY);
    request = overlay_request(1u, 1, 1, 10);
    request.owner = owner;
    CHECK(ui_overlay_show(&fixture.runtime, 102u, &request, NULL).code ==
          UI_ERR_INVALID_ARGUMENT);
    request = overlay_request(1u, 1, 1, 10);
    request.timeout_ms = 1u;
    CHECK(ui_overlay_show(&fixture.runtime, 102u, &request, NULL).code ==
          UI_ERR_INVALID_ARGUMENT);

    request = overlay_request(1u, 1, 1, 10);
    CHECK(ui_overlay_show(&fixture.runtime, 102u, &request, &notice).code ==
          UI_OK);
    state = find_overlay_state(&fixture.overlay_probes[1], 1);
    CHECK(state != NULL);
    CHECK(!lv_obj_has_flag(
        state->interactive_child, LV_OBJ_FLAG_CLICKABLE));
    CHECK(ui_overlay_is_valid(&fixture.runtime, notice));
    CHECK(!fixture.overlay_probes[1].callback_error);
    lv_test_reset();
    return true;
}

static bool test_noninteractive_overlay_enforced_without_render(void)
{
    fixture_t fixture;
    ui_overlay_request_t request;
    overlay_state_t *state;

    fixture_prepare(&fixture, 1u, 1u, false);
    fixture_enable_overlays(&fixture);
    fixture.overlay_adapter.render = NULL;
    fixture.overlay_probes[2].enable_click_on_activate = true;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    request = overlay_request(1u, 0, 1, 10);
    CHECK(ui_overlay_show(&fixture.runtime, 103u, &request, NULL).code ==
          UI_OK);
    state = find_overlay_state(&fixture.overlay_probes[2], 1);
    CHECK(state != NULL && state->interactive_child != NULL);
    CHECK(!lv_obj_has_flag(
        state->interactive_child, LV_OBJ_FLAG_CLICKABLE));
    CHECK(!fixture.overlay_probes[2].callback_error);
    lv_test_reset();
    return true;
}

static bool test_illegal_overlay_root_delete_enters_fail_stop(void)
{
    fixture_t fixture;
    ui_overlay_request_t request;
    ui_overlay_handle_t notice;
    overlay_state_t *state;
    uint16_t event_count;

    fixture_prepare(&fixture, 1u, 1u, false);
    fixture_enable_overlays(&fixture);
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);
    request = overlay_request(1u, 1, 1, 10);
    CHECK(ui_overlay_show(&fixture.runtime, 102u, &request, &notice).code ==
          UI_OK);
    state = find_overlay_state(&fixture.overlay_probes[1], 1);
    CHECK(state != NULL && state->root != NULL);
    event_count = fixture.observer.count;

    lv_obj_del(state->root);
    CHECK(ui_runtime_get_state(&fixture.runtime) ==
          UI_RUNTIME_STATE_FAULTED);
    CHECK(!ui_overlay_is_valid(&fixture.runtime, notice));
    CHECK(state->unmount_count[UI_OVERLAY_UNMOUNT_DISMISSED] == 0u);
    CHECK(state->dismiss_count == 0u);
    CHECK(fixture.overlay_probes[1].destroy_count == 0u);
    ui_runtime_tick(&fixture.runtime, 1u);
    CHECK(fixture.observer.count == (uint16_t)(event_count + 1u));
    CHECK(fixture.observer.events[event_count].kind == UI_RUNTIME_FAULTED);
    lv_test_reset();
    return true;
}

static bool test_overlay_descriptor_validation(void)
{
    fixture_t fixture;

    fixture_prepare(&fixture, 1u, 1u, false);
    fixture_enable_overlays(&fixture);
    fixture.overlay_descs[2].pointer_interactive = true;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code ==
          UI_ERR_INVALID_ARGUMENT);
    fixture.overlay_descs[2].pointer_interactive = false;
    fixture.overlay_descs[2].default_transient_ms = 0u;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code ==
          UI_ERR_INVALID_ARGUMENT);
    fixture.overlay_descs[2].default_transient_ms = 100u;
    fixture.config.modal_capacity = 0u;
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code ==
          UI_ERR_INVALID_ARGUMENT);
    CHECK(lv_test_object_count() == 0u);
    lv_test_reset();
    return true;
}

static bool test_100k_mixed_overlay_operations(void)
{
    fixture_t fixture;
    ui_overlay_handle_t transient = { 0 };
    ui_overlay_handle_t modal = { 0 };
    ui_overlay_handle_t notices[UI_RUNTIME_MAX_OVERLAYS] = { 0 };
    int transient_id = 0;
    uint32_t random_state = UINT32_C(0xA53C9E17);
    uint32_t step;

    fixture_prepare(&fixture, 1u, 1u, false);
    fixture_enable_overlays(&fixture);
    CHECK(ui_runtime_init(&fixture.runtime, &fixture.config).code == UI_OK);

    for(step = 1u; step <= 100000u; ++step) {
        uint32_t choice = random_next(&random_state) % 8u;
        ui_overlay_request_t request;
        ui_result_t result;
        uint16_t i;

        switch(choice) {
            case 0u:
                {
                int requested_id = (int)step;
                request = overlay_request(
                    step, 0, requested_id, (int)step);
                request.timeout_ms = (uint16_t)(step % 9u + 1u);
                result = ui_overlay_show(
                    &fixture.runtime, 103u, &request, &transient);
                CHECK(result.code == UI_OK || result.code == UI_ERR_BUSY);
                if(result.code == UI_OK) transient_id = requested_id;
                break;
                }
            case 1u:
                if(ui_overlay_is_valid(&fixture.runtime, transient)) {
                    request = overlay_request(
                        step, 0, transient_id, (int)(step * 2u));
                    result = ui_overlay_update(
                        &fixture.runtime, transient, &request.args);
                    CHECK(result.code == UI_OK);
                }
                break;
            case 2u:
                if(ui_overlay_is_valid(&fixture.runtime, transient)) {
                    result = ui_overlay_dismiss(
                        &fixture.runtime, transient);
                    CHECK(result.code == UI_OK || result.code == UI_ERR_BUSY);
                }
                break;
            case 3u:
                request = overlay_request(
                    step, (int16_t)(step % 5u),
                    (int)step, (int)step);
                result = ui_overlay_show(
                    &fixture.runtime, 102u, &request, NULL);
                CHECK(result.code == UI_OK ||
                      result.code == UI_ERR_BUSY ||
                      result.code == UI_ERR_CAPACITY_FULL);
                if(result.code == UI_OK) {
                    ui_overlay_handle_t handle;
                    result = ui_overlay_show(
                        &fixture.runtime, 102u, &request, &handle);
                    CHECK(result.code == UI_OK);
                    for(i = 0; i < ARRAY_COUNT(notices); ++i) {
                        if(!ui_overlay_is_valid(
                               &fixture.runtime, notices[i])) {
                            notices[i] = handle;
                            break;
                        }
                    }
                }
                break;
            case 4u:
                i = (uint16_t)(
                    random_next(&random_state) % ARRAY_COUNT(notices));
                if(ui_overlay_is_valid(&fixture.runtime, notices[i])) {
                    result = ui_overlay_dismiss(
                        &fixture.runtime, notices[i]);
                    CHECK(result.code == UI_OK || result.code == UI_ERR_BUSY);
                }
                break;
            case 5u:
                if(ui_overlay_is_valid(&fixture.runtime, modal)) {
                    CHECK(ui_nav_back(&fixture.runtime).code ==
                          UI_OK_NO_CHANGE);
                }
                else {
                    request = overlay_request(
                        step, 1, (int)step, (int)step);
                    result = ui_overlay_show(
                        &fixture.runtime, 101u, &request, &modal);
                    CHECK(result.code == UI_OK || result.code == UI_ERR_BUSY);
                }
                break;
            case 6u:
                CHECK(ui_runtime_suspend(&fixture.runtime).code ==
                      UI_OK_TRANSITIONING);
                ui_runtime_tick(&fixture.runtime, step * 10u);
                CHECK(ui_runtime_resume(&fixture.runtime).code ==
                      UI_OK_TRANSITIONING);
                break;
            default: {
                ui_entry_handle_t top;
                CHECK(ui_entry_get_top(&fixture.runtime, &top).code == UI_OK);
                CHECK(ui_entry_invalidate(&fixture.runtime, top).code == UI_OK);
                break;
            }
        }

        ui_runtime_tick(&fixture.runtime, step * 10u + 1u);
        CHECK(ui_runtime_get_state(&fixture.runtime) ==
              UI_RUNTIME_STATE_RUNNING);
        CHECK(lv_test_object_count() < 64u);
        CHECK(!fixture.overlay_probes[0].callback_error);
        CHECK(!fixture.overlay_probes[1].callback_error);
        CHECK(!fixture.overlay_probes[2].callback_error);
    }
    lv_test_reset();
    return true;
}

typedef bool (*test_fn_t)(void);

typedef struct {
    const char *name;
    test_fn_t run;
} test_case_t;

int main(void)
{
    static const test_case_t tests[] = {
        { "init, invalidate, root back", test_init_invalidate_and_root_back },
        { "push rebuild then pop", test_push_rebuild_then_pop },
        { "retain, reclaim, restore", test_retain_reclaim_and_restore },
        { "mount failure rollback", test_mount_failure_rolls_back_at_tick },
        { "existing mount failure", test_existing_target_mount_failure_preserves_entry },
        { "create failure ownership", test_create_failure_does_not_destroy_unowned_state },
        { "full stack replace/reset", test_full_stack_still_allows_replace_and_reset },
        { "pop-to and launch modes", test_pop_to_single_top_and_single_task },
        { "suspend during transaction", test_suspend_during_transaction_and_resume },
        { "fullscreen status", test_fullscreen_controls_status_visibility },
        { "observer reentrancy", test_started_observer_cannot_reenter_navigation },
        { "illegal delete fail-stop", test_illegal_slot_delete_enters_fail_stop },
        { "illegal delete in mount", test_illegal_slot_delete_during_mount_faults },
        { "reparented slot fail-stop", test_reparented_slot_is_detected_at_safe_tick },
        { "external screen load fail-stop", test_external_screen_load_is_detected_at_safe_tick },
        { "deleted initial focus", test_deleted_initial_focus_does_not_leave_raw_pointer },
        { "illegal delete in unmount", test_illegal_delete_during_unmount_stops_cleanup },
        { "cross-slot delete in cleanup", test_cross_slot_delete_during_normal_cleanup_faults },
        { "delete event tick reentrancy", test_child_delete_event_cannot_reenter_runtime_tick },
        { "init mount failure", test_init_mount_failure_preserves_bootstrap },
        { "status mount failure", test_status_mount_failure_rolls_back_page },
        { "host creation failure", test_host_creation_failure_rolls_back_and_restores_group },
        { "thread and config validation", test_thread_check_and_invalid_configuration },
        { "slide transition completion", test_slide_transition_waits_for_completion },
        { "exit fade stale completion", test_exit_fade_and_stale_completion_are_isolated },
        { "transition start degradation", test_transition_start_failure_snaps_degraded },
        { "transition watchdog wrap", test_transition_watchdog_wraparound },
        { "suspend snaps transition", test_suspend_snaps_transition_without_degradation },
        { "modal back and navigation", test_modal_back_and_navigation_blocking },
        { "notice priority and FIFO", test_notice_priority_preemption_and_fifo_resume },
        { "transient latest-wins", test_transient_latest_wins_and_timeout },
        { "transient timeout wrap", test_transient_timeout_wraparound },
        { "overlay same-key update", test_overlay_same_key_update_and_immutable_fields },
        { "entry-owned overlay", test_entry_owned_overlay_closes_on_navigation },
        { "overlay suspend and input", test_overlay_suspend_resume_and_input_rules },
        { "overlay failure and capacity", test_overlay_mount_failure_capacity_and_consume_back },
        { "overlay validation and input", test_overlay_request_validation_and_notice_input },
        { "overlay input without render", test_noninteractive_overlay_enforced_without_render },
        { "overlay root fail-stop", test_illegal_overlay_root_delete_enters_fail_stop },
        { "overlay descriptor validation", test_overlay_descriptor_validation },
        { "100k random state transitions", test_deterministic_random_state_machine },
        { "100k mixed overlay operations", test_100k_mixed_overlay_operations },
    };
    size_t i;

    for(i = 0; i < ARRAY_COUNT(tests); ++i) {
        if(!tests[i].run()) {
            fprintf(stderr, "FAILED: %s\n", tests[i].name);
            lv_test_reset();
            return 1;
        }
        printf("PASS: %s\n", tests[i].name);
    }
    printf("%zu tests passed; ui_runtime_t=%zu bytes\n",
           ARRAY_COUNT(tests), sizeof(ui_runtime_t));
    return 0;
}
