#ifndef UI_RUNTIME_UI_RUNTIME_H
#define UI_RUNTIME_UI_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_RUNTIME_ABI_VERSION 1u

#ifndef UI_RUNTIME_MAX_DEPTH
#define UI_RUNTIME_MAX_DEPTH 16u
#endif

#ifndef UI_RUNTIME_MAX_INDEVS
#define UI_RUNTIME_MAX_INDEVS 4u
#endif

#ifndef UI_RUNTIME_MAX_ROUTES
#define UI_RUNTIME_MAX_ROUTES 256u
#endif

#ifndef UI_RUNTIME_MAX_OVERLAYS
#define UI_RUNTIME_MAX_OVERLAYS 8u
#endif

#ifndef UI_RUNTIME_MAX_OVERLAY_TYPES
#define UI_RUNTIME_MAX_OVERLAY_TYPES 64u
#endif

#ifndef UI_RUNTIME_TRANSITION_WATCHDOG_MARGIN_MS
#define UI_RUNTIME_TRANSITION_WATCHDOG_MARGIN_MS 250u
#endif

/*
 * The object is opaque but caller-owned. The larger 64-bit allowance is for
 * host-side validation; normal 32-bit targets reserve 2 KiB.
 */
#ifndef UI_RUNTIME_STORAGE_SIZE
#if UINTPTR_MAX > UINT32_MAX
#define UI_RUNTIME_STORAGE_SIZE 3072u
#else
#define UI_RUNTIME_STORAGE_SIZE 2048u
#endif
#endif

#if LVGL_VERSION_MAJOR >= 9
typedef lv_display_t ui_lv_display_t;
#else
typedef lv_disp_t ui_lv_display_t;
#endif

typedef struct ui_view_slot ui_view_slot_t;
typedef struct ui_page_adapter ui_page_adapter_t;
typedef struct ui_route_desc ui_route_desc_t;
typedef struct ui_status_bar_adapter ui_status_bar_adapter_t;
typedef struct ui_overlay_adapter ui_overlay_adapter_t;
typedef struct ui_overlay_desc ui_overlay_desc_t;
typedef struct ui_overlay_request ui_overlay_request_t;

typedef enum {
    UI_OK = 0,
    UI_OK_TRANSITIONING,
    UI_OK_NO_CHANGE,
    UI_OK_REENTERED,
    UI_BACK_UNHANDLED,
    UI_ERR_NOT_INITIALIZED,
    UI_ERR_SUSPENDED,
    UI_ERR_FAULTED,
    UI_ERR_WRONG_THREAD,
    UI_ERR_REENTRANT,
    UI_ERR_BUSY,
    UI_ERR_MODAL_ACTIVE,
    UI_ERR_UNKNOWN_ROUTE,
    UI_ERR_INVALID_ENTRY,
    UI_ERR_INVALID_OVERLAY,
    UI_ERR_BACK_STACK_FULL,
    UI_ERR_CAPACITY_FULL,
    UI_ERR_STATE_CREATE,
    UI_ERR_VIEW_MOUNT,
    UI_ERR_HOST,
    UI_ERR_GENERATION_EXHAUSTED,
    UI_ERR_UNSUPPORTED,
    UI_ERR_INVALID_ARGUMENT,
} ui_result_code_t;

typedef struct {
    ui_result_code_t code;
    uint32_t txn_id;
} ui_result_t;

typedef struct {
    const void *data;
    uint16_t size;
} ui_args_t;

typedef struct {
    uint32_t runtime_generation;
    uint16_t slot;
    uint16_t reserved;
    uint32_t entry_generation;
} ui_entry_handle_t;

typedef struct {
    uint32_t runtime_generation;
    uint16_t slot;
    uint16_t reserved;
    uint32_t overlay_generation;
} ui_overlay_handle_t;

typedef enum {
    UI_HOST_SHELL = 0,
    UI_HOST_FULLSCREEN,
} ui_host_kind_t;

typedef enum {
    UI_VIEW_RETAIN = 0,
    UI_VIEW_REBUILD,
} ui_view_policy_t;

typedef enum {
    UI_LAUNCH_STANDARD = 0,
    UI_LAUNCH_SINGLE_TOP,
    UI_LAUNCH_SINGLE_TASK,
} ui_launch_mode_t;

typedef enum {
    UI_TRANSITION_NONE = 0,
    UI_TRANSITION_SLIDE_LEFT,
    UI_TRANSITION_SLIDE_RIGHT,
    UI_TRANSITION_SLIDE_UP,
    UI_TRANSITION_SLIDE_DOWN,
    UI_TRANSITION_FADE,
} ui_transition_kind_t;

typedef struct {
    ui_transition_kind_t kind;
    uint16_t duration_ms;
    uint16_t delay_ms;
} ui_transition_desc_t;

typedef enum {
    UI_UNMOUNT_REBUILD = 0,
    UI_UNMOUNT_REMOVED,
    UI_UNMOUNT_RECLAIM,
    UI_UNMOUNT_SHUTDOWN,
} ui_unmount_reason_t;

typedef enum {
    UI_ACTIVATE_NEW = 0,
    UI_ACTIVATE_BACK,
    UI_ACTIVATE_REENTER,
    UI_ACTIVATE_DISPLAY_RESUME,
} ui_activate_reason_t;

typedef enum {
    UI_DEACTIVATE_COVERED = 0,
    UI_DEACTIVATE_REMOVED,
    UI_DEACTIVATE_DISPLAY_SUSPEND,
} ui_deactivate_reason_t;

typedef enum {
    UI_BACK_RESULT_UNHANDLED = 0,
    UI_BACK_RESULT_CONSUMED,
} ui_back_result_t;

typedef enum {
    UI_OVERLAY_LANE_NOTICE = 0,
    UI_OVERLAY_LANE_MODAL,
    UI_OVERLAY_LANE_TRANSIENT,
} ui_overlay_lane_t;

typedef enum {
    UI_MODAL_BACK_DISMISS = 0,
    UI_MODAL_BACK_CONSUME,
} ui_modal_back_policy_t;

typedef enum {
    UI_OVERLAY_DISMISS_EXPLICIT = 0,
    UI_OVERLAY_DISMISS_OWNER_DEACTIVATED,
    UI_OVERLAY_DISMISS_REPLACED,
    UI_OVERLAY_DISMISS_TIMEOUT,
    UI_OVERLAY_DISMISS_ACTIVATION_FAILED,
} ui_overlay_dismiss_reason_t;

typedef enum {
    UI_OVERLAY_ACTIVATE_NEW = 0,
    UI_OVERLAY_ACTIVATE_PREEMPTED,
    UI_OVERLAY_ACTIVATE_RESUME,
    UI_OVERLAY_ACTIVATE_DISPLAY_RESUME,
} ui_overlay_activate_reason_t;

typedef enum {
    UI_OVERLAY_DEACTIVATE_PREEMPTED = 0,
    UI_OVERLAY_DEACTIVATE_DISMISSED,
    UI_OVERLAY_DEACTIVATE_DISPLAY_SUSPEND,
} ui_overlay_deactivate_reason_t;

typedef enum {
    UI_OVERLAY_UNMOUNT_PREEMPTED = 0,
    UI_OVERLAY_UNMOUNT_DISMISSED,
} ui_overlay_unmount_reason_t;

typedef ui_result_code_t (*ui_overlay_create_state_fn)(
    void *ctx, const ui_args_t *initial_args, void **out_state);
typedef void (*ui_overlay_destroy_state_fn)(void *ctx, void *state);
typedef ui_result_code_t (*ui_overlay_mount_view_fn)(
    void *ctx, ui_view_slot_t *slot, void *state);
typedef void (*ui_overlay_unmount_view_fn)(
    void *ctx, ui_view_slot_t *slot, void *state,
    ui_overlay_unmount_reason_t reason);
typedef void (*ui_overlay_activate_fn)(
    void *ctx, ui_view_slot_t *slot, void *state,
    ui_overlay_activate_reason_t reason);
typedef void (*ui_overlay_deactivate_fn)(
    void *ctx, ui_view_slot_t *slot, void *state,
    ui_overlay_deactivate_reason_t reason);
typedef void (*ui_overlay_render_fn)(
    void *ctx, ui_view_slot_t *slot, void *state);
typedef ui_result_code_t (*ui_overlay_update_state_fn)(
    void *ctx, void *state, const ui_args_t *args);
typedef void (*ui_overlay_dismiss_fn)(
    void *ctx, void *state, ui_overlay_dismiss_reason_t reason);

struct ui_overlay_adapter {
    uint16_t abi_version;
    uint16_t struct_size;
    ui_overlay_create_state_fn create_state;
    ui_overlay_destroy_state_fn destroy_state;
    ui_overlay_mount_view_fn mount_view;
    ui_overlay_unmount_view_fn unmount_view;
    ui_overlay_activate_fn on_activate;
    ui_overlay_deactivate_fn on_deactivate;
    ui_overlay_render_fn render;
    ui_overlay_update_state_fn update_state;
    ui_overlay_dismiss_fn on_dismiss;
};

struct ui_overlay_desc {
    uint16_t abi_version;
    uint16_t struct_size;
    uint16_t overlay_type;
    ui_overlay_lane_t lane;
    bool pointer_interactive;
    ui_modal_back_policy_t modal_back_policy;
    uint16_t default_transient_ms;
    const ui_overlay_adapter_t *adapter;
    void *user_ctx;
};

typedef enum {
    UI_OVERLAY_OWNER_RUNTIME = 0,
    UI_OVERLAY_OWNER_ENTRY,
} ui_overlay_owner_kind_t;

struct ui_overlay_request {
    uint32_t key;
    int16_t priority;
    ui_overlay_owner_kind_t owner_kind;
    ui_entry_handle_t owner;
    uint16_t timeout_ms;
    ui_args_t args;
};

typedef ui_result_code_t (*ui_page_create_state_fn)(
    void *ctx, const ui_args_t *initial_args, void **out_state);
typedef void (*ui_page_destroy_state_fn)(void *ctx, void *state);
typedef ui_result_code_t (*ui_page_mount_view_fn)(
    void *ctx, ui_view_slot_t *slot, void *state);
typedef void (*ui_page_unmount_view_fn)(
    void *ctx, ui_view_slot_t *slot, void *state,
    ui_unmount_reason_t reason);
typedef void (*ui_page_activate_fn)(
    void *ctx, ui_view_slot_t *slot, void *state,
    ui_activate_reason_t reason, const ui_args_t *reenter_args);
typedef void (*ui_page_deactivate_fn)(
    void *ctx, ui_view_slot_t *slot, void *state,
    ui_deactivate_reason_t reason);
typedef void (*ui_page_render_fn)(
    void *ctx, ui_view_slot_t *slot, void *state);
typedef ui_back_result_t (*ui_page_back_fn)(
    void *ctx, ui_view_slot_t *slot, void *state);

struct ui_page_adapter {
    uint16_t abi_version;
    uint16_t struct_size;
    ui_page_create_state_fn create_state;
    ui_page_destroy_state_fn destroy_state;
    ui_page_mount_view_fn mount_view;
    ui_page_unmount_view_fn unmount_view;
    ui_page_activate_fn on_activate;
    ui_page_deactivate_fn on_deactivate;
    ui_page_render_fn render;
    ui_page_back_fn on_back;
};

#define UI_PAGE_ADAPTER_INITIALIZER                                      \
    { UI_RUNTIME_ABI_VERSION, (uint16_t)sizeof(ui_page_adapter_t),       \
      NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }

struct ui_route_desc {
    uint16_t abi_version;
    uint16_t struct_size;
    uint16_t route_id;
    uint16_t status_style;
    ui_host_kind_t host_kind;
    ui_view_policy_t view_policy;
    ui_launch_mode_t launch_mode;
    ui_transition_desc_t enter_transition;
    ui_transition_desc_t exit_transition;
    const ui_page_adapter_t *adapter;
    void *user_ctx;
};

typedef enum {
    UI_STATUS_VISIBILITY_ROUTE = 0,
    UI_STATUS_VISIBILITY_DISPLAY_SUSPEND,
    UI_STATUS_VISIBILITY_DISPLAY_RESUME,
} ui_status_visibility_reason_t;

typedef ui_result_code_t (*ui_status_bar_mount_fn)(
    void *ctx, ui_view_slot_t *slot);
typedef void (*ui_status_bar_unmount_fn)(
    void *ctx, ui_view_slot_t *slot);
typedef void (*ui_status_bar_set_visible_fn)(
    void *ctx, bool visible, ui_status_visibility_reason_t reason);
typedef void (*ui_status_bar_apply_style_fn)(void *ctx, uint16_t style);

struct ui_status_bar_adapter {
    uint16_t abi_version;
    uint16_t struct_size;
    ui_status_bar_mount_fn mount;
    ui_status_bar_unmount_fn unmount;
    ui_status_bar_set_visible_fn set_visible;
    ui_status_bar_apply_style_fn apply_style;
};

typedef enum {
    UI_RUNTIME_STATE_UNINITIALIZED = 0,
    UI_RUNTIME_STATE_STARTING,
    UI_RUNTIME_STATE_RUNNING,
    UI_RUNTIME_STATE_SUSPEND_PENDING,
    UI_RUNTIME_STATE_SUSPENDED,
    UI_RUNTIME_STATE_RESUME_PENDING,
    UI_RUNTIME_STATE_FAULTED,
} ui_runtime_state_t;

typedef enum {
    UI_RECLAIM_ONE_OLDEST = 0,
    UI_RECLAIM_ALL_HIDDEN,
} ui_reclaim_mode_t;

typedef enum {
    UI_NAV_STARTED = 0,
    UI_NAV_FAILED,
    UI_NAV_COMPLETED,
    UI_NAV_COMPLETED_DEGRADED,
    UI_RUNTIME_SUSPENDED,
    UI_RUNTIME_RESUMED,
    UI_RUNTIME_FAULTED,
} ui_runtime_event_kind_t;

typedef enum {
    UI_EVENT_DETAIL_NONE = 0,
    UI_EVENT_DETAIL_TRANSITION_TIMEOUT = 1u << 0,
    UI_EVENT_DETAIL_HOST_SNAP = 1u << 1,
} ui_event_detail_flags_t;

typedef struct {
    uint32_t runtime_generation;
    uint32_t txn_id;
    ui_runtime_event_kind_t kind;
    uint16_t from_route_id;
    uint16_t to_route_id;
    ui_entry_handle_t from;
    ui_entry_handle_t to;
    bool from_handle_valid;
    bool to_handle_valid;
    ui_result_code_t code;
    uint32_t detail_flags;
} ui_runtime_event_t;

typedef void (*ui_runtime_observer_fn)(
    void *ctx, const ui_runtime_event_t *event);
typedef bool (*ui_runtime_thread_check_fn)(void *ctx);

typedef union {
    max_align_t align;
    uint8_t bytes[UI_RUNTIME_STORAGE_SIZE];
} ui_runtime_t;

#ifdef __cplusplus
#define UI_RUNTIME_INITIALIZER {}
#else
#define UI_RUNTIME_INITIALIZER { .bytes = { 0 } }
#endif

typedef struct {
    uint16_t abi_version;
    uint16_t struct_size;
    const ui_route_desc_t *routes;
    uint16_t route_count;
    const ui_overlay_desc_t *overlays;
    uint16_t overlay_type_count;
    uint16_t initial_route_id;
    ui_args_t initial_args;
    uint16_t max_depth;
    uint16_t modal_capacity;
    uint16_t notice_capacity;
    uint16_t transient_capacity;
    ui_lv_display_t *display;
    lv_indev_t *const *indevs;
    uint8_t indev_count;
    const ui_status_bar_adapter_t *status_bar;
    void *status_bar_ctx;
    lv_coord_t status_bar_height;
    ui_runtime_observer_fn observer;
    void *observer_ctx;
    ui_runtime_thread_check_fn is_ui_thread;
    void *thread_ctx;
} ui_runtime_config_t;

lv_obj_t *ui_view_slot_content(ui_view_slot_t *slot);
ui_result_code_t ui_view_slot_focus_add(
    ui_view_slot_t *slot, lv_obj_t *object);
ui_result_code_t ui_view_slot_focus_initial(
    ui_view_slot_t *slot, lv_obj_t *object);

ui_result_t ui_runtime_init(
    ui_runtime_t *runtime, const ui_runtime_config_t *config);
void ui_runtime_tick(ui_runtime_t *runtime, uint32_t now_ms);
uint32_t ui_runtime_generation(const ui_runtime_t *runtime);
ui_runtime_state_t ui_runtime_get_state(const ui_runtime_t *runtime);

ui_result_t ui_runtime_suspend(ui_runtime_t *runtime);
ui_result_t ui_runtime_resume(ui_runtime_t *runtime);

ui_result_t ui_nav_push(
    ui_runtime_t *runtime, uint16_t route_id, const ui_args_t *args,
    ui_entry_handle_t *out);
ui_result_t ui_nav_pop(ui_runtime_t *runtime);
ui_result_t ui_nav_replace(
    ui_runtime_t *runtime, uint16_t route_id, const ui_args_t *args,
    ui_entry_handle_t *out);
ui_result_t ui_nav_reset(
    ui_runtime_t *runtime, uint16_t route_id, const ui_args_t *args,
    ui_entry_handle_t *out);
ui_result_t ui_nav_pop_to(
    ui_runtime_t *runtime, ui_entry_handle_t target);
ui_result_t ui_nav_back(ui_runtime_t *runtime);

ui_result_t ui_overlay_show(
    ui_runtime_t *runtime, uint16_t overlay_type,
    const ui_overlay_request_t *request, ui_overlay_handle_t *out);
ui_result_t ui_overlay_update(
    ui_runtime_t *runtime, ui_overlay_handle_t handle,
    const ui_args_t *args);
ui_result_t ui_overlay_dismiss(
    ui_runtime_t *runtime, ui_overlay_handle_t handle);
bool ui_overlay_is_valid(
    const ui_runtime_t *runtime, ui_overlay_handle_t handle);

ui_result_t ui_entry_invalidate(
    ui_runtime_t *runtime, ui_entry_handle_t handle);
ui_result_t ui_entry_get_top(
    const ui_runtime_t *runtime, ui_entry_handle_t *out);
ui_result_t ui_entry_find_topmost(
    const ui_runtime_t *runtime, uint16_t route_id,
    ui_entry_handle_t *out);
bool ui_entry_is_valid(
    const ui_runtime_t *runtime, ui_entry_handle_t handle);

ui_result_t ui_runtime_reclaim_views(
    ui_runtime_t *runtime, ui_reclaim_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif
