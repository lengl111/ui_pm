# ui_runtime

`ui_runtime` is the standalone LVGL page-management module described in
[`../UI_PAGE_MANAGER_DESIGN.md`](../UI_PAGE_MANAGER_DESIGN.md).

The dependency direction is deliberately one-way:

```text
application or validation project -> ui_runtime -> public LVGL interface
```

The module never includes files from `lv_pc_a`, `ref/unigui`, or another
application. `lv_pc_a` links this directory as an external module for the
real-LVGL integration smoke and selected unigui View migration tests.

Declare Runtime storage with `UI_RUNTIME_INITIALIZER` (or clear it to zero)
before the first `ui_runtime_init()` call. Route and adapter descriptors must
remain immutable for the Runtime lifetime. The configured `is_ui_thread`
callback is mandatory; all Runtime calls and Adapter callbacks run in that UI
context.

`UI_RUNTIME_MAX_DEPTH`, `UI_RUNTIME_MAX_INDEVS`,
`UI_RUNTIME_MAX_OVERLAYS`, `UI_RUNTIME_TRANSITION_WATCHDOG_MARGIN_MS`, and
`UI_RUNTIME_STORAGE_SIZE` are build-wide configuration macros. If overridden,
the same definitions must be used when compiling the library and every caller.
The implementation has a static size/alignment assertion; the build system is
still responsible for keeping those definitions consistent.

The current implementation milestone covers the page-transition and overlay
core:

- one RootScreen with shell/fullscreen hosts and a runtime-owned ViewSlot;
- fixed Entry pool and bounded back stack;
- push, pop, replace, reset, pop-to, back, singleTop, and singleTask;
- state/View separation with RETAIN and REBUILD;
- transactional preparation, rollback, delayed finalization, observer, finite
  root transitions, completion callbacks, and a wrap-safe transition watchdog;
- invalidation, hidden-View reclamation, suspend/resume, focus scopes, and
  fail-stop guards for runtime-owned LVGL roots.
- fixed Notice, Modal, and Transient lanes with priority/FIFO arbitration,
  Modal input ownership, latest-wins Transient timeout, same-key state update,
  Entry ownership, and safe-Tick cleanup.

Animated transitions are owned by RootHost. A transition keeps the Runtime
busy until LVGL reports completion or the watchdog deadline is reached. A
startup failure or watchdog timeout snaps to the committed target and emits
`UI_NAV_COMPLETED_DEGRADED` with detail flags; it never runs page cleanup from
an LVGL animation callback.

The current core reserves 2,048 bytes on 32-bit targets and 3,072 bytes on
64-bit validation hosts. At the default limits (`MAX_DEPTH=16`,
`MAX_INDEVS=4`, `MAX_OVERLAYS=8`), the measured implementation state is 1,836
bytes in the 32-bit compile model and 2,584 bytes in the 64-bit test build.
This is Runtime metadata only; LVGL objects and Adapter-owned state are
separate.

## Interface contract

- Initialize `ui_runtime_t` with `UI_RUNTIME_INITIALIZER`, then call
  `ui_runtime_init()` from a safe UI context. Descriptors and their `user_ctx`
  values remain immutable and alive for the Runtime lifetime.
- Call `ui_runtime_tick(runtime, now_ms)` after the current
  `lv_timer_handler()` and input dispatch have returned. All managed root
  deletion and terminal observer delivery happens there.
- `UI_TRANSITION_NONE` requires zero duration and delay. Every animated
  transition requires a nonzero duration. Push/replace/reset use the target
  Route's enter descriptor; pop/pop-to/singleTask use the source Route's exit
  descriptor. Slide names describe the direction in which content travels.
- LVGL 8 builds with `LV_USE_USER_DATA=0` still support
  `UI_TRANSITION_NONE`, but reject animated Route descriptors with
  `UI_ERR_UNSUPPORTED`; immutable animation tokens are required to reject
  stale completion callbacks safely. LVGL 9 always provides this field.
- The watchdog interval is `delay + duration +
  UI_RUNTIME_TRANSITION_WATCHDOG_MARGIN_MS` (250 ms by default). Override the
  macro build-wide only; Runtime and callers must use the same definition.
- `ui_args_t` memory is borrowed only for the current call. An Adapter copies
  anything it needs after that call.
- A successful `mount_view` receives exactly one `unmount_view`; a failed
  mount receives none. The Adapter rolls back its own non-LVGL side effects,
  while Runtime deletes the complete partial Slot at a safe Tick.
- Page navigation is single-transaction and never queued. A second structural
  request returns `UI_ERR_BUSY` until finalization.
- Repeating `(overlay_type, key)` updates the existing state and View without
  changing its priority, owner, FIFO identity, or handle. Re-showing a
  Transient also refreshes its timeout.
- Notice/Modal capacity includes live and retiring instances. Transient has
  logical capacity one plus one internal candidate Slot so latest-wins can
  prepare the replacement before invalidating the current item.
- While a lane waits for safe cleanup, new show/dismiss operations on that lane
  return `UI_ERR_BUSY`; an already-live instance can still update its state.
- Modal blocks page navigation and owns keypad/encoder focus. Notice may accept
  pointer input only when its descriptor allows it. Runtime strips pointer
  flags from Transient and non-interactive Notice subtrees after mount/render.
- Route/Overlay handles identify instances, not descriptor types. A handle is
  invalid immediately after its Entry/Overlay is logically removed, even
  though physical cleanup may wait for the next Tick.

## Build the independent tests

```sh
cmake -S ui_runtime -B ui_runtime/build -DUI_RUNTIME_BUILD_TESTS=ON
cmake --build ui_runtime/build
ctest --test-dir ui_runtime/build --output-on-failure
```

The tests link a minimal Fake LVGL implementation and do not use `lv_pc_a`.
They cover deterministic lifecycle and failure cases plus 100,000 generated
page operations and 100,000 mixed Overlay operations. Production integration should leave
`UI_RUNTIME_BUILD_TESTS=OFF` (the default) and link an existing `lvgl` or
`lvgl::lvgl` CMake target.
