#ifndef UI_RUNTIME_INTERNAL_H
#define UI_RUNTIME_INTERNAL_H

#include <limits.h>
#include <string.h>

#include "ui_runtime/ui_runtime.h"
#include "ui_lvgl_compat.h"

#define UI_RUNTIME_MAGIC UINT32_C(0x55525431)
#define UI_INDEX_NONE UINT16_MAX

#if defined(__GNUC__) || defined(__clang__)
#define UI_RUNTIME_MAY_ALIAS __attribute__((__may_alias__))
#else
#define UI_RUNTIME_MAY_ALIAS
#endif

typedef struct ui_runtime_impl ui_runtime_impl_t;

typedef enum {
    UI_SLOT_PAGE = 0,
    UI_SLOT_STATUS_BAR,
    UI_SLOT_OVERLAY,
} ui_slot_kind_t;

struct ui_view_slot {
    ui_runtime_impl_t *owner;
    lv_obj_t *root;
    lv_group_t *focus_group;
    uint16_t owner_index;
    uint8_t kind;
    bool borrowed;
    bool mounted;
};

typedef struct ui_host_transition_callback ui_host_transition_callback_t;

typedef struct {
    ui_runtime_impl_t *owner;
    lv_obj_t *source_root;
    lv_obj_t *target_root;
    uint32_t runtime_generation;
    uint32_t txn_id;
    uint32_t deadline_ms;
    uint16_t source_index;
    uint16_t target_index;
    ui_host_transition_callback_t *callback;
    uint8_t kind;
    bool entering;
    bool active;
    bool animation_running;
    bool completion_pending;
    bool deadline_armed;
} ui_host_transition_t;

struct ui_host_transition_callback {
    ui_host_transition_t *transition;
    ui_runtime_impl_t *owner;
    uint32_t runtime_generation;
    uint32_t txn_id;
    bool active;
};

typedef struct {
    lv_obj_t *root_screen;
    lv_obj_t *shell_host;
    lv_obj_t *content_host;
    lv_obj_t *fullscreen_host;
    lv_obj_t *overlay_host;
    lv_obj_t *notice_host;
    lv_obj_t *modal_host;
    lv_obj_t *transient_host;
    lv_obj_t *input_shield;
    ui_view_slot_t status_slot;
    lv_group_t *empty_group;
    lv_coord_t width;
    lv_coord_t height;
    lv_coord_t status_height;
    ui_host_transition_t transition;
    ui_host_transition_callback_t transition_callbacks[2];
    uint8_t transition_callback_index;
    uint16_t applied_status_style;
    bool status_mounted;
    bool status_visible;
    bool status_visibility_valid;
    bool status_style_valid;
    bool loaded;
} ui_root_host_t;

typedef enum {
    UI_ENTRY_FREE = 0,
    UI_ENTRY_CANDIDATE,
    UI_ENTRY_LIVE,
    UI_ENTRY_RETIRED,
} ui_entry_role_t;

typedef enum {
    UI_ENTRY_INACTIVE = 0,
    UI_ENTRY_ACTIVE,
} ui_entry_lifecycle_t;

typedef enum {
    UI_VIEW_NONE = 0,
    UI_VIEW_MOUNTED_HIDDEN,
    UI_VIEW_VISIBLE,
    UI_VIEW_DELETE_PENDING,
} ui_entry_view_state_t;

typedef struct {
    const ui_route_desc_t *route;
    void *state;
    ui_view_slot_t view;
    uint32_t generation;
    uint32_t last_active_seq;
    uint8_t role;
    uint8_t lifecycle;
    uint8_t view_state;
    bool state_owned;
    bool dirty;
} ui_entry_t;

typedef enum {
    UI_OVERLAY_FREE = 0,
    UI_OVERLAY_LIVE,
    UI_OVERLAY_RETIRED,
} ui_overlay_role_t;

typedef struct {
    const ui_overlay_desc_t *desc;
    void *state;
    ui_view_slot_t view;
    ui_entry_handle_t owner;
    uint32_t generation;
    uint32_t fifo_sequence;
    uint32_t deadline_ms;
    uint32_t key;
    int16_t priority;
    uint8_t role;
    uint8_t owner_kind;
    bool state_owned;
    bool active;
    bool ever_activated;
    bool display_suspended;
    bool deadline_valid;
    bool dismiss_notified;
    bool cleanup_pending;
    bool preempt_cleanup;
    ui_overlay_dismiss_reason_t dismiss_reason;
} ui_overlay_t;

typedef struct {
    uint16_t current;
    uint16_t capacity;
} ui_overlay_lane_state_t;

typedef enum {
    UI_TXN_NONE = 0,
    UI_TXN_PREPARING,
    UI_TXN_PREPARE_FAILED,
    UI_TXN_COMMITTED,
} ui_txn_phase_t;

typedef enum {
    UI_NAV_OP_PUSH = 0,
    UI_NAV_OP_POP,
    UI_NAV_OP_REPLACE,
    UI_NAV_OP_RESET,
    UI_NAV_OP_POP_TO,
    UI_NAV_OP_SINGLE_TASK,
} ui_nav_op_t;

typedef struct {
    uint32_t id;
    ui_runtime_event_t terminal_event;
    uint16_t source_index;
    uint16_t target_index;
    uint16_t retired_count;
    uint16_t retired[UI_RUNTIME_MAX_DEPTH];
    uint8_t phase;
    uint8_t operation;
    bool target_is_new;
    bool source_is_covered;
} ui_nav_txn_t;

struct UI_RUNTIME_MAY_ALIAS ui_runtime_impl {
    uint32_t magic;
    uint32_t runtime_generation;
    uint32_t entry_generation_counter;
    uint32_t txn_counter;
    uint32_t active_sequence;
    ui_runtime_state_t state;

    const ui_route_desc_t *routes;
    uint16_t route_count;
    uint16_t max_depth;
    uint16_t stack_depth;
    uint16_t stack[UI_RUNTIME_MAX_DEPTH];
    ui_entry_t entries[UI_RUNTIME_MAX_DEPTH + 1u];

    const ui_overlay_desc_t *overlay_descs;
    uint16_t overlay_type_count;
    ui_overlay_t overlays[UI_RUNTIME_MAX_OVERLAYS + 1u];
    ui_overlay_lane_state_t overlay_lanes[3];
    uint32_t overlay_generation_counter;
    uint32_t overlay_fifo_counter;
    uint32_t now_ms;
    bool now_ms_valid;

    ui_root_host_t host;
    ui_nav_txn_t txn;

    ui_lv_display_t *display;
    lv_indev_t *indevs[UI_RUNTIME_MAX_INDEVS];
    lv_group_t *previous_groups[UI_RUNTIME_MAX_INDEVS];
    uint8_t indev_count;

    const ui_status_bar_adapter_t *status_bar;
    void *status_bar_ctx;
    ui_runtime_observer_fn observer;
    void *observer_ctx;
    ui_runtime_thread_check_fn is_ui_thread;
    void *thread_ctx;

    uint8_t callback_depth;
    uint8_t pending_reclaim;
    lv_obj_t *normal_delete_root;
    bool in_tick;
    bool fault_event_pending;
    bool fault_event_delivered;
};

static inline ui_runtime_impl_t *ui_runtime_impl(ui_runtime_t *runtime)
{
    return (ui_runtime_impl_t *)(void *)runtime->bytes;
}

static inline const ui_runtime_impl_t *ui_runtime_impl_const(
    const ui_runtime_t *runtime)
{
    return (const ui_runtime_impl_t *)(const void *)runtime->bytes;
}

static inline ui_result_t ui_result(ui_result_code_t code, uint32_t txn_id)
{
    ui_result_t result;
    result.code = code;
    result.txn_id = txn_id;
    return result;
}

void ui_runtime_latch_fault(ui_runtime_impl_t *impl);
void ui_runtime_callback_enter(ui_runtime_impl_t *impl);
void ui_runtime_callback_leave(ui_runtime_impl_t *impl);

ui_result_code_t ui_root_host_init(
    ui_runtime_impl_t *impl, lv_coord_t status_bar_height);
ui_result_code_t ui_root_host_mount_status(ui_runtime_impl_t *impl);
void ui_root_host_rollback(ui_runtime_impl_t *impl);
ui_result_code_t ui_root_host_load(ui_runtime_impl_t *impl);
ui_result_code_t ui_root_host_create_page_slot(
    ui_runtime_impl_t *impl, ui_entry_t *entry, uint16_t entry_index);
ui_result_code_t ui_root_host_create_overlay_slot(
    ui_runtime_impl_t *impl, ui_overlay_t *overlay,
    uint16_t overlay_index);
void ui_root_host_delete_slot(
    ui_runtime_impl_t *impl, ui_view_slot_t *slot);
void ui_root_host_show_page(
    ui_runtime_impl_t *impl, ui_entry_t *target);
void ui_root_host_hide_page(ui_entry_t *entry);
ui_result_code_t ui_root_host_start_page_transition(
    ui_runtime_impl_t *impl, ui_entry_t *source, ui_entry_t *target,
    const ui_transition_desc_t *transition, bool entering,
    uint32_t txn_id, bool *start_degraded);
bool ui_root_host_transition_is_ready(
    const ui_runtime_impl_t *impl);
bool ui_root_host_transition_timed_out(
    ui_runtime_impl_t *impl, uint32_t now_ms);
ui_result_code_t ui_root_host_finish_page_transition(
    ui_runtime_impl_t *impl);
void ui_root_host_show_overlay(
    ui_runtime_impl_t *impl, ui_overlay_t *overlay);
void ui_root_host_hide_overlay(ui_overlay_t *overlay);
void ui_root_host_enforce_overlay_input(ui_overlay_t *overlay);
void ui_root_host_block_input(ui_runtime_impl_t *impl);
void ui_root_host_restore_input(
    ui_runtime_impl_t *impl, ui_entry_t *active);
void ui_root_host_restore_input_group(
    ui_runtime_impl_t *impl, lv_group_t *focus_group);
void ui_root_host_set_status_suspended(
    ui_runtime_impl_t *impl, bool suspended);
bool ui_root_host_validate(const ui_runtime_impl_t *impl);

void ui_overlay_owner_deactivated(
    ui_runtime_impl_t *impl, ui_entry_handle_t owner);
void ui_overlay_tick(ui_runtime_impl_t *impl, uint32_t now_ms);
void ui_overlay_suspend(ui_runtime_impl_t *impl);
void ui_overlay_resume(ui_runtime_impl_t *impl);
ui_result_code_t ui_overlay_back(ui_runtime_impl_t *impl);
ui_result_code_t ui_overlay_navigation_blocker(
    const ui_runtime_impl_t *impl);
void ui_overlay_restore_input(ui_runtime_impl_t *impl);
ui_entry_t *ui_runtime_top_entry(ui_runtime_impl_t *impl);
bool ui_runtime_is_ui_thread(const ui_runtime_impl_t *impl);
bool ui_runtime_entry_handle_valid(
    const ui_runtime_impl_t *impl, ui_entry_handle_t handle);
bool ui_runtime_overlay_handle_valid(
    const ui_runtime_impl_t *impl, ui_overlay_handle_t handle);

#endif
