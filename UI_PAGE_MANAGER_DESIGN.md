# LVGL 通用 UI Runtime / Page Manager 设计

状态：V2 设计基线；独立模块已完成 M3 页面转场、Watchdog 与 Overlay 核心，并通过真实 LVGL 9.3 PC 及选定 unigui View 集成验证；目标板验证仍待完成  
适用：单显示设备、LVGL 8/9 风格的嵌入式 UI。  
目标：用固定、可预估的核心元数据和尽量小的公共接口，覆盖普通页面、全屏页面、状态栏、弹窗、返回栈、生命周期和内存回收。

设计原则只有一句：**页面管理器管理页面身份、位置和切换机制，不理解页面里装了什么业务。** 它类似管理衣架和衣架位置，不检查衣服的颜色、材质或口袋内容。

### 仓库落地边界

生产实现独立放在仓库根目录的 `ui_runtime/`，不放入任何具体应用或验证工程：

```text
ui_runtime/   通用页面管理模块、公共头文件和独立测试
lv_pc_a/      LVGL PC 端集成及 unigui 迁移验证，不拥有 Runtime 源码
ref/          只读参考实现，不作为生产模块依赖
```

依赖方向固定为 `应用/验证工程 -> ui_runtime -> LVGL 公开接口`。`ui_runtime` 不引用 `lv_pc_a`、`ref/unigui` 或任何产品页面；独立测试使用最小 Fake LVGL 验证状态机和所有权，真实 LVGL 行为再由 `lv_pc_a` 做集成验证。

当前 `ui_runtime/` 已实现 RootHost/ViewSlot、固定 Entry 池、push/pop/replace/reset/pop_to、singleTop/singleTask、RETAIN/REBUILD、失败回滚、invalidate/reclaim、suspend/resume、状态栏整体管理、focus scope、observer、Overlay 三 lane、页面根转场与 Watchdog，以及非法根删除/reparent/换 screen 的 fail-stop。真实 LVGL 9.3 PC 集成已经验证，真实目标板仍属于后续里程碑；动画启动失败会明确报告 `COMPLETED_DEGRADED`，不会静默退化。

## 1. 最终结构

采用“单 RootScreen + 固定 Host + 单返回栈 + 事务式导航”。应用只使用一个 `UiRuntime` 公共模块；`PageManager`、`RootHost` 和 `OverlayManager` 都是其私有实现。

```text
UiRuntime（每个 display 一个）
└── RootScreen（Runtime 生命周期内只加载一次）
    ├── ShellHost
    │   ├── StatusBarSlot
    │   └── ContentHost
    │       └── Page ViewSlot -> PageAdapter 创建的内容子树
    ├── FullscreenHost
    │   └── Page ViewSlot -> PageAdapter 创建的内容子树
    └── OverlayHost
        ├── NoticeHost
        ├── ModalHost
        ├── TransientHost
        └── InputShield（仅在转场/suspend 时启用，透明且位于最上层）
```

页面不是独立的 LVGL screen。Runtime 不在每次导航时调用 `lv_screen_load()`，只在固定 Host 内准备、显示、隐藏和删除 Runtime 自己拥有的 ViewSlot。

本设计固定以下取舍：

1. 返回栈中每个页面实例是一个 Entry；相同 Route 可以有多个 Entry。
2. 页面留在栈内时可以选择保留 View 或仅保留 state；退出栈后 View 和 state 一律销毁。
3. 不提供退出栈后的通用 ViewCache；昂贵图片、解码器、相机流等由业务资源模块复用。
4. 导航事务不排队；旧事务未完成时，新结构导航直接返回 `UI_ERR_BUSY`。
5. Route/Overlay descriptor 在 Runtime 启动前注册并保持只读；实例参数、模型和显示数据可以动态变化。
6. 所有 LVGL 受管根对象都由 RootHost 创建和删除；Adapter 只构建其内部内容。
7. 所有 LVGL 根对象删除都发生在安全 Tick，而不是 LVGL 事件或动画回调栈内。
8. 物理按键去抖、线程消息、状态栏字段和业务资源压力都不进入 PageManager。

### 1.1 与 MVP/MVU 的组合定案

`ui_runtime` 与 MVP/MVU 是互补模块，不是两个竞争的页面管理器。最终采用
“页面机制独立、表现策略可选、对外可以统一”的组合方式：

```text
应用 Facade（可选，只有统一调用入口，不含第二套状态机）
├── ui_runtime（产品级页面机制，每个 display 一个）
│   └── Entry / ViewSlot -> PageAdapter
│                           ├── 直接 LVGL 页面
│                           ├── MVP Feature
│                           └── MVU Feature
└── RTAO / HRT / Services（业务与系统数据）
```

必须遵守以下规则：

1. `ui_runtime` 不依赖 MVP 或 MVU；PageAdapter 通过应用适配接缝选择其中
   一种，或直接使用 LVGL。一个产品可以按 Route 混用三种方式。
2. 一个产品页面只有一个页面宿主、一个根 ViewSlot 和一个显隐控制者，均由
   `ui_runtime`/RootHost 负责。MVP/MVU 不得创建 LVGL screen、删除 Slot 根
   对象或自行管理产品级返回栈。
3. MVP/MVU 的职责限于 Feature 表现状态：Snapshot 投影、草稿、pending、
   错误、用户意图、Command/Effect 和页面内部 Panel/Mode/步骤。
4. Feature 的 activate/deactivate 在组合模式下只表示恢复/暂停订阅、动画
   和前台工作，不得再次 hide/show ViewSlot 页面根对象。
5. 页面内容 render 也必须只有一个所有者。直接 LVGL 页面使用 PageAdapter
   的 `render + ui_entry_invalidate()`；现有 MVP/MVU 页面由自身在 active 时
   render，PageAdapter 的 `render` 留空，避免重复投影。
6. MVP/MVU 页面隐藏期间只更新 Feature 状态而不 render；重新激活或重建时
   由 Feature 执行一次完整 render。页面内部内容子树可以按 Model 显隐。
7. MVU 的 `screen` 只能是页面内部的 Panel/Mode/向导步骤。产品级 Route、
   Back、Overlay、Fullscreen 和页面回收必须由 `ui_runtime` 管理。
8. Feature 只能产生产品级导航意图；应用必须在当前 Presenter/Update/Effect
   调用返回后执行 Runtime 导航，必要时投递到下一安全 Tick，不能从 Runtime
   生命周期回调或同步 Feature 回调中重入导航。

不采用“在 MVP/MVU 内直接实现一套通用 ui_runtime”的方式。若 MVP 和 MVU
各自实现，会产生两套不一致的栈和生命周期；若在 MVP/MVU 包中再抽一个共同
页面核心，逻辑上仍然是 `ui_runtime`，但会错误地让通用页面机制依赖表现层，
并失去直接 LVGL 页面和无屏项目的独立复用能力。

是否引入 `ui_runtime` 由页面机制需求决定，而不是由页面数量决定：

| 产品场景 | 推荐组合 |
|---|---|
| 单页面或少量页面内部 Panel | MVP/MVU + LVGL，直接使用表现层即可 |
| 需要产品级返回栈、Overlay、Fullscreen、统一生命周期或 View 回收 | `ui_runtime` + 每个 Route 选择直接 LVGL、MVP 或 MVU |
| 无屏产品 | HRT/RTAO + Services，不引入 UI 模块 |

这相当于 Android 中将 `NavController` 与 `ViewModel/Presenter` 分开：
`ui_runtime` 管“页面在哪里、何时存在”，MVP/MVU 管“页面显示什么以及如何
与业务服务交换”。

本节是组合架构的冻结结论。当前 `ui_runtime` 已有通用 PageAdapter 接口，
MVP/MVU 自行 render 可以直接保留；尚需提供 hosted View 适配，使 deactivate
不隐藏 Runtime 的 ViewSlot 页面，并保证 PageAdapter 不配置重复 render。在
该适配及集成测试完成前，不得将现有会自动隐藏根容器的 LVGL Adapter 原样
嵌入 Route。

## 2. 模块职责

| 模块 | 负责 | 明确不负责 |
|---|---|---|
| `UiRuntime` | 组合内部模块、公共准入、observer、suspend/resume、fault | 业务任务、跨线程队列、物理输入去抖 |
| `PageManager` | Entry、单返回栈、导航事务、逻辑生命周期 | LVGL 对象、图片、网络、相机、页面字段 |
| `RootHost` | RootScreen/Host/ViewSlot、层级、转场、焦点、LVGL 输入闸门、安全删除 | Route 查找、返回栈语义、物理按键解释、业务生命周期决定 |
| `OverlayManager` | 三 lane 实例、排序、owner、输入仲裁、原子交换 | 弹窗文案含义、按钮业务、任务取消和业务超时 |
| `PageAdapter` | 页面内容子树、Entry state、View 绑定和局部状态 | 切 screen、删除 ViewSlot、修改全局输入 group |
| `StatusBarAdapter` | 状态栏内容、数据订阅、去重、局部动画 | 页面栈和导航结果 |
| `InputAdapter` | 按键/旋钮/touch 归一化、去抖、长按、release/cancel | 页面栈和转场 |
| `UiDispatcher` | 跨线程投递、命令容量、payload 生命周期和 epoch | 页面 View 所有权 |
| 业务资源模块 | bitmap、缩略图、解码、DMA、相机/音频/网络任务 | 页面栈和 Host 层级 |

一个新需求只有满足下列至少一项，才考虑进入 Runtime 核心：

- 改变 Entry 身份或返回栈结构；
- 改变受管 ViewSlot 的所有权、层级或可见性；
- 参与页面、Modal 与全局输入焦点之间的仲裁；
- 是所有页面都必须遵守、且由 Runtime 才能统一保证的生命周期机制。

否则应留在 Adapter、Dispatcher、InputAdapter 或业务模块。页面内部列表、选择步骤、局部菜单和局部弹出控件都属于页面；只有跨页面、覆盖公共区域或参与全局 Modal 仲裁的对象才进入 OverlayManager。

## 3. Route 与 PageAdapter

### 3.1 Route descriptor

Route descriptor 在 `ui_runtime_init()` 前以只读数组注册，不要求调用方排序。Runtime 在初始化时检查重复 ID 和非法组合，运行时做有固定上限的线性查找，不建立动态索引或哈希表。页面导航是低频操作，典型几十个 Route 的比较成本远小于 View 创建；线性表减少 RAM 和配置约束。只有实测查找成为问题时，才由构建工具生成直接索引，不改变公共接口。

```c
typedef enum {
    UI_HOST_SHELL,       /* 自动使用 StatusBar + ContentHost */
    UI_HOST_FULLSCREEN,  /* 填满 FullscreenHost，不显示 StatusBar */
} ui_host_kind_t;

typedef enum {
    UI_VIEW_RETAIN,      /* Entry 被覆盖后隐藏保留，可被显式回收 */
    UI_VIEW_REBUILD,     /* Entry 被覆盖后删除 ViewSlot，保留 state */
} ui_view_policy_t;

typedef enum {
    UI_LAUNCH_STANDARD,
    UI_LAUNCH_SINGLE_TOP,
    UI_LAUNCH_SINGLE_TASK,
} ui_launch_mode_t;
```

每个 descriptor 至少包含：

- `route_id`、`host_kind`、`view_policy`、`launch_mode`；
- Shell 页面使用的有限 `status_style` 枚举；
- RootHost 使用的转场类型、duration 和 delay；
- 一个只读 `PageAdapter` 操作表和 `user_ctx`。

`host_kind` 直接决定状态栏整体显隐，不再额外提供 `status_mode`：Shell 页面显示状态栏，Fullscreen 页面不显示。确实出现“不带状态栏但又不能使用 FullscreenHost”的第三种稳定布局后，再新增明确的 Host 类型，而不是制造两个可冲突开关。

新 Route 默认选择 `UI_VIEW_REBUILD`。只有实测重建时延不可接受，并且常驻 View RAM 已计入产品预算后，才选择 `UI_VIEW_RETAIN`。

### 3.2 精简生命周期接口

```c
typedef struct ui_view_slot ui_view_slot_t;

typedef enum {
    UI_UNMOUNT_REBUILD,
    UI_UNMOUNT_REMOVED,
    UI_UNMOUNT_RECLAIM,
    UI_UNMOUNT_SHUTDOWN,
} ui_unmount_reason_t;

typedef enum {
    UI_ACTIVATE_NEW,
    UI_ACTIVATE_BACK,
    UI_ACTIVATE_REENTER,
    UI_ACTIVATE_DISPLAY_RESUME,
} ui_activate_reason_t;

typedef enum {
    UI_DEACTIVATE_COVERED,
    UI_DEACTIVATE_REMOVED,
    UI_DEACTIVATE_DISPLAY_SUSPEND,
} ui_deactivate_reason_t;

typedef enum {
    UI_BACK_RESULT_UNHANDLED,
    UI_BACK_RESULT_CONSUMED,
} ui_back_result_t;

ui_result_code_t create_state(void *ctx,
                              const ui_args_t *initial_args,
                              void **out_state);
void destroy_state(void *ctx, void *state);

ui_result_code_t mount_view(void *ctx,
                            ui_view_slot_t *slot,
                            void *state);
void unmount_view(void *ctx,
                  ui_view_slot_t *slot,
                  void *state,
                  ui_unmount_reason_t reason);

void on_activate(void *ctx,
                 ui_view_slot_t *slot,
                 void *state,
                 ui_activate_reason_t reason,
                 const ui_args_t *reenter_args);
void on_deactivate(void *ctx,
                   ui_view_slot_t *slot,
                   void *state,
                   ui_deactivate_reason_t reason);

void render(void *ctx, ui_view_slot_t *slot, void *state); /* optional */
ui_back_result_t on_back(void *ctx,
                         ui_view_slot_t *slot,
                         void *state);                     /* optional */
```

生命周期原因是有限枚举：

- activate：`NEW`、`BACK`、`REENTER`、`DISPLAY_RESUME`；
- deactivate：`COVERED`、`REMOVED`、`DISPLAY_SUSPEND`；
- unmount：`REBUILD`、`REMOVED`、`RECLAIM`、可选健康关闭时的 `SHUTDOWN`。

`SINGLE_TOP` 命中当前 Entry 时会再次收到 `on_activate(REENTER, args)`，但不会先收到 deactivate，也不会创建导航事务。`SINGLE_TASK` 命中栈内 Entry 时，在其重新成为栈顶后收到同一个 `REENTER` 原因。这样一个回调同时覆盖 Android 中 resume/new-intent 一类需求，不再为每个细小阶段增加接口。

`create_state/destroy_state` 必须成对提供或同时为空。未提供时 `state == NULL` 是合法状态。调用前 Runtime 把 `*out_state` 清零；失败时 Adapter 必须保持其为空并清理尚未转移的资源。创建成功后，即使返回的 state 为空，Runtime 最终仍调用一次配对的 `destroy_state(ctx, state)`。`mount_view` 必须提供；其他通知回调可以为空，等价于 no-op。

`args` 只在当前回调期间有效；Adapter 必须复制需要长期保存的内容。`create_state` 只允许返回 `UI_OK` 或状态创建错误，`mount_view` 只允许返回 `UI_OK` 或挂载错误，不能把业务数据是否可用编码成 Runtime 错误。

通知回调不返回导航成败。它们必须在 UI 线程内有界完成；诊断由 Adapter 自己记录，不能把状态栏数据、页面 render 或业务订阅错误伪装成导航失败。

## 4. Runtime-owned ViewSlot

ViewSlot 是本次精简最重要的所有权规则：

1. RootHost 先在目标 Host 下创建一个受管根 `lv_obj_t`，登记删除守卫和内部状态。
2. Runtime 把一个借用的 `ui_view_slot_t *` 交给 Adapter。
3. Adapter 只能在 Slot 的内容 parent 下创建页面内容，不能删除、reparent 或缓存 Slot/内容 parent 作为全局身份。
4. Runtime 最终先调用 `unmount_view()`，再由 RootHost 删除整个 Slot 子树。

RootHost 创建 Slot 时统一清除滚动、边框、padding 和默认圆角，设置透明背景、裁剪规则以及填满对应 Host 的尺寸。PageAdapter 不重复初始化这些容器属性，只负责内容子树；需要滚动、圆角或页面背景时在 Slot 内再建自己的内容对象。这样不同页面不会因 LVGL 默认主题差异改变 Host 布局。

Adapter 可通过受限辅助接口取得内容 parent、登记焦点对象和设置初始焦点：

```c
lv_obj_t *ui_view_slot_content(ui_view_slot_t *slot);
ui_result_code_t ui_view_slot_focus_add(ui_view_slot_t *slot,
                                        lv_obj_t *object);
ui_result_code_t ui_view_slot_focus_initial(ui_view_slot_t *slot,
                                            lv_obj_t *object);
```

这些引用只在对应回调及本次 mounted 周期内有效，不是页面查询接口。`focus_add/initial` 只接受当前 Slot 的后代对象，否则返回参数错误。普通业务代码仍不能通过 Entry handle 获得 Slot、state 或裸 `lv_obj_t *`。

### mount 成败契约

- `mount_view()` 成功后，Runtime 保证在删除 Slot 前恰好调用一次 `unmount_view()`，且传入有效 Slot。
- `mount_view()` 失败时，Runtime**不会**调用 `unmount_view()`。Adapter 返回前必须撤销本次建立的非 LVGL 订阅、timer 和外部资源引用。
- mount 失败时已经创建的 LVGL 子对象都留在 Slot 下；Adapter 不在当前回调栈删除它们。RootHost 在下一安全 Tick 删除整个 Slot。
- state 至少存活到 Slot 删除返回。新 Entry 的 state 随后销毁；栈内旧 Entry 的 state 保留，以便以后再次 mount。
- 栈内旧 Entry 的 mount 失败不能消费或破坏不可回滚的持久 state；本次临时 View 绑定撤销后，同一 state 必须能在以后再次 mount。
- `unmount_view()` 用于保存滚动位置等局部 View 状态、停止会访问子树的 timer/回调、释放 View 绑定资源并清空子对象引用。它不得删除 Slot。
- `destroy_state()` 不能依赖一个仍 mounted 的 Slot，且必须允许该 Entry 从未 mount 成功；mount 失败路径中的 state 字段也必须处于可销毁状态。

推荐 Adapter 把 mount 写成明确的构造事务：先创建完全归属于 Slot 的 LVGL 子树并完成所有可能失败的校验，最后才登记外部订阅和独立 timer。若最后一步失败，只同步撤销这些外部副作用；已创建的 LVGL 子树留给 RootHost 删除。这样无需让 unmount 理解半构造状态。

RootHost 不为纯 touch 页面预先分配 `lv_group_t`；只有第一次登记 focus 对象时才按需创建 scope。删除 Slot 时先把受管 indev 切到空 scope，再调用 unmount，随后解除对象与 group 的关系、删除 group，最后删除 Slot 根对象。具体顺序必须用目标 LVGL 版本的集成测试确认。

这消除了旧方案中“`create_view` 失败但必须返回部分根对象”和 `before_view_destroy(NULL)` 两个容易写错的契约。故障路径不再要求 Adapter 猜测一个根对象是否仍然存在。

ViewSlot 会让每个已挂载页面或 Overlay 多一个普通 `lv_obj_t`，通常是几十到数百字节的 LVGL 元数据；它不复制 framebuffer、draw buffer 或整屏像素。这个小成本换来单一、可证明的根对象所有权。

### 4.1 初始化与失败回滚

Runtime 状态只有：

```text
UNINITIALIZED -> STARTING -> RUNNING -> SUSPEND_PENDING -> SUSPENDED
                            ^                                  |
                            └──────── RESUME_PENDING <─────────┘

任一已初始化状态 --所有权/Host 致命错误--> FAULTED
```

`resume()` 可以在安全 Tick 前把 `SUSPEND_PENDING` 直接取消回 RUNNING；同理，`suspend()` 可以把尚未执行的 `RESUME_PENDING` 取消回 SUSPENDED。`STARTING` 只存在于同步 init 调用内部。FAULTED 是终态。

`ui_runtime_init()` 必须在 UI 安全上下文调用，即当前不在 LVGL event/timer/animation 回调栈中。它先完成 descriptor、容量、display 和 indev 参数校验，再依次创建 RootScreen/Host、可选 StatusBarSlot、初始 Entry state 和初始 Page ViewSlot。初始页面 mount 成功后才 mount StatusBar，随后加载 RootScreen，执行 `on_activate(NEW) -> render`，最后进入 RUNNING，不发布普通导航事件。把 StatusBar mount 放在页面准备之后，可以让其失败沿同一条初始化反向回滚路径处理。初始化期间输入仍被禁用，旧 active screen 直到成功切换前保持不变。

初始化前的 active screen 始终由应用拥有。Runtime 在自身完全准备成功前不切走它；成功加载 RootScreen 后也不擅自删除它。应用若使用一次性 bootstrap screen，应在 init 成功且确认它已非 active 后自行删除。这样初始化失败能保持原 UI 可见，也避免 Runtime 猜测外部 screen 的资源所有权。

初始化失败时没有任何公共 handle，也不调用 observer。因为调用点本身已经是约定的安全上下文，Runtime 可以按正常所有权顺序同步反向回滚：对成功 mount 的对象调用 unmount，删除 Slot/Host/RootScreen，最后销毁对应 state。mount 失败的 Slot 不调用 unmount。RootScreen 加载成功后的 activate/render 都是不可失败通知，因此正常错误回滚不会发生在它们之后；若其中触发 fatal，则进入 FAULTED 而不是伪装成普通 init 错误。全部普通回滚后恢复为 UNINITIALIZED，调用方可以修正配置后重试；失败过程中消耗过的 generation 不回退。

“运行期间删除只在安全 Tick”与上述初始化回滚不冲突：前者防止从不可控事件栈删除对象，后者的调用前提本身就是安全点。

## 5. Entry、handle 与返回栈

Entry 是一次页面访问实例，不等同于 Route。固定槽位至少保存：

```text
route_id
slot_index + uint32 generation
opaque state
ui_view_slot_t *view_slot（可为空）
slot_role: FREE / CANDIDATE / LIVE / RETIRED
logical_lifecycle: ACTIVE / INACTIVE
view_state: NONE / MOUNTED / INCOMING / VISIBLE / HIDDEN / DELETE_PENDING
handle_valid
dirty
```

公共 handle 由 Runtime generation、slot 和 Entry generation 组成。全零 handle 永远无效：

- 只有仍在返回栈中的 `LIVE` Entry handle 有效，即使 ViewSlot 已被回收；
- Entry 在提交点退出返回栈后 handle 立即失效，资源可以保持 `RETIRED` 到安全 Tick；
- 迟到的 invalidate、Overlay owner 和异步 UI 命令都必须校验 generation；
- Runtime 不提供“按 Route 或 handle 取得页面 View/state”的接口。

Entry 池大小固定为 `UI_RUNTIME_MAX_DEPTH + 1`。前 `max_depth` 是有效返回栈上限，额外一个槽位只用于候选页面，因此满栈时 `replace/reset` 仍能先准备新页面再原子提交。

只有真正新增 Entry 的 `push` 受深度上限限制。栈满时返回 `UI_ERR_BACK_STACK_FULL`，已有界面保持不变；命中已有 Entry 的 `SINGLE_TOP/SINGLE_TASK`、`replace`、`reset` 和返回操作仍可工作。Runtime 不偷偷删除历史页面。

回到根使用当前栈底 Entry；根页面的 Back 返回 `UI_BACK_UNHANDLED`，由应用决定锁屏、退出或忽略。`reset` 成功后，新 Entry 成为新根，旧根不再参与递归返回。

## 6. 导航事务

同一时刻最多一个页面结构事务。事务分为准入、准备、提交和安全收尾。

### 6.1 准入

普通导航按固定顺序检查：Runtime 是否 RUNNING、UI 线程、回调重入、导航 BUSY、活动 Modal、参数/Route/handle、栈容量。失败不创建事务，也不发布 `STARTED`。固定检查顺序同时定义多个条件都不满足时返回哪个错误，正式测试不能依赖偶然的实现分支顺序。

`back` 在通过 Runtime/BUSY 检查后先交给 Modal；无 Modal 时调用活动页面 `on_back()`，未消费才执行 pop。

Runtime 不保存“下一次导航”。转场或失败清理未结束时，重复请求统一返回 `UI_ERR_BUSY`。InputAdapter 必须把一次物理按压归一化成一次逻辑动作，并按产品策略决定 release/cancel 前是否允许 repeat；长按和连按策略同样属于 InputAdapter。这样 Runtime 解决事务重入，InputAdapter 解决物理去抖，两层职责不混合。

准入成功后先分配非零 `txn_id`、置 BUSY、关闭页面/Overlay focus，对 Runtime 接管的 LVGL indev 调用 RootHost 端口的 `input_quiesce()`，再同步发布 `STARTED`，然后进入准备阶段。observer 在 `STARTED` 中只能观察事务，不能取得尚未提交的候选 Entry handle。

### 6.2 准备

新 Entry 的准备顺序固定：

1. 保留一个 `CANDIDATE` 槽位并生成新 generation；
2. 创建 Adapter state；
3. RootHost 在目标 Host 中创建候选 ViewSlot；
4. Adapter `mount_view(slot, state)`，建立能够安全显示的完整结构。

恢复栈内 Entry 时跳过 state 创建，只在 Slot 已回收时重新创建并 mount；仍保留的 Slot 不需要准备操作。准备阶段不调用 `on_activate`，因此失败不会产生虚假的前台生命周期。

`create_state` 或 `mount_view` 失败时不修改原返回栈和当前页面。候选 Slot 在下一安全 Tick 删除，清理结束前 Runtime 保持 BUSY；随后 RootHost 恢复原活动页面或 Modal 的 focus/hit-test，再发布 `FAILED`。mount 失败的新 state 在 Slot 删除后销毁；恢复失败的旧 state 继续留在原 Entry。导航方法会立即返回具体错误和已经分配的 `txn_id`，但同一事务的异步终态仍只由 observer 发布一次。

`render` 是 void 通知。`mount_view` 必须留下可安全显示的最小内容，render 应幂等且有界；业务数据显示异常由 Adapter 自己诊断，不把导航标记为 degraded。每次 Entry activate 后固定 render 一次，不依赖 dirty；ACTIVE Entry 的重复 invalidate 通过 dirty 合并到后续安全 Tick。

### 6.3 提交

准备成功后一次性提交：

1. 在一个不调用外部代码的提交段内，原子修改返回栈、ACTIVE/INACTIVE 标志和 handle；被移除 Entry handle 立即失效，候选 Entry handle 此时生效；
2. 同一提交段把源 Entry 拥有的 Overlay 标记为逻辑关闭，原因是 `OWNER_DEACTIVATED`，其 handle 立即失效；
3. RootHost 把目标 Slot 布置为 incoming 并保持 InputShield；
4. 原活动 Entry 根据操作收到 `on_deactivate(COVERED/REMOVED)`；
5. 目标 Entry 收到 `on_activate(NEW/BACK/REENTER)`；
6. Runtime 调用目标 `render()` 并消费 dirty；`REENTER` 参数因此一定先写入 Adapter state；
7. RootHost 启动转场。

Adapter 生命周期通知不能否决已经准备成功的事务。通知期间允许的只读查询看到的已经是提交后的栈和 handle 状态，不会观察到“已 deactivate 但仍 ACTIVE”的中间状态。页面输入在整个转场期间由 RootHost 关闭，页面不得自行抢焦点或启动页面根转场。

`on_deactivate` 只对应 ACTIVE -> INACTIVE 边迁移。`pop_to/reset` 中已经 inactive 的中间 Entry 不会为了销毁再收到一次 deactivate，它们只走 `unmount(REMOVED) -> destroy_state`；对应 Entry-owner Overlay 在它们首次失去前台时已经关闭。

### 6.4 安全收尾

动画完成或 Watchdog 超时后，下一安全 Tick 执行：

- 目标 Slot 吸附为 visible；
- 被覆盖的 `RETAIN` Slot 隐藏；
- 被覆盖的 `REBUILD` Slot 执行 `unmount(REBUILD)` 后删除，Entry/state 保留；
- pop/replace/reset 移除的 Entry 执行 `unmount(REMOVED)`、删除 Slot、再 `destroy_state`；
- RootHost 把 keypad/encoder focus scope 交给活动 Modal，否则交给目标页面；
- 恢复输入并发布 `COMPLETED` 或 `COMPLETED_DEGRADED`。

每个 Entry 内严格保持 `unmount -> lv_obj_delete(slot) 返回 -> destroy_state`。一次移除多个 Entry 时按原栈从顶到底处理；批量上限只能在两个 Entry 之间切 Tick，不能拆开单个 Entry 的收尾顺序。

`COMPLETED*` 只等待本事务拥有的页面和因 owner 失效而关闭的 Overlay 收尾，以及 Host/focus/input 恢复；不等待独立 View 回收、Runtime-owner Overlay、状态栏数据或任何业务任务。

### 6.5 操作语义

| 操作 | 语义 |
|---|---|
| `push` | 新建 Entry 并压栈 |
| `pop` | 恢复栈顶下方 Entry，成功准备后移除旧栈顶 |
| `replace` | 成功准备候选后原子替换栈顶 |
| `reset` | 成功准备候选后将其设为新根，旧栈统一退休 |
| `pop_to(handle)` | 保留目标 Entry，移除其上全部 Entry |
| `back` | Modal -> 页面 `on_back` -> pop -> 根页面交给应用 |
| `SINGLE_TOP` | 栈顶同 Route 时调用 `on_activate(REENTER)`，不建事务 |
| `SINGLE_TASK` | 命中栈内同 Route 时清除其上 Entry，并以 `REENTER` 激活 |

launch mode 只影响 `push`。`replace/reset` 是调用方明确表达的结构操作，始终创建新 Entry。

`SINGLE_TOP` reenter 自动把当前 Entry 置 dirty，返回 `UI_OK_REENTERED` 和原 handle；下一安全 Tick 合并执行一次 render。需要精确等待页面根转场结束的少数业务，不增加全体页面的 `didAppear` 生命周期，而是由应用 observer 根据 `COMPLETED + active handle` 投递页面动作。普通 View 动画可以在 activate/render 中启动并随页面一起进入转场。

## 7. 转场、重复请求与 Watchdog

转场由 RootHost 独占：

- Route 只声明有限转场枚举、duration 和 delay；Adapter 不直接动画页面 Slot 根对象；
- `push/replace/reset` 使用目标 Route 的 enter 描述，`pop/pop_to/SINGLE_TASK` 使用源 Route 的 exit 描述；`SINGLE_TOP` 不产生根转场；
- 动画完成回调只记录 `runtime_generation + txn_id + completion_pending`；删除和 observer 交付留到安全 Tick；
- 完成标志重复、过期或 generation 不匹配时直接忽略；
- LVGL 8 若编译为 `LV_USE_USER_DATA=0`，Runtime 仍支持 `NONE`，但在初始化时以 `UI_ERR_UNSUPPORTED` 拒绝动画 descriptor；不可变动画 token 是可靠过滤迟到回调的必要条件；
- Watchdog deadline 为 `delay + duration + 全局余量`，使用 32 位回绕安全比较；
- 动画启动失败、完成回调丢失或超时后，RootHost 强制吸附到已提交目标并恢复全部不变量；
- 只有上述**导航机制故障**可以产生 `COMPLETED_DEGRADED`。

页面 render、状态栏数据、网络结果或图片解码失败不能改变导航终态。提交前 ViewSlot mount 失败是 `FAILED`；提交后 Host 无法按计划完成动画但成功吸附，才是 `COMPLETED_DEGRADED`。

“degraded”成立的前提是 RootHost 最终仍恢复了唯一可见目标、正确层级和输入归属。如果连强制吸附都不能恢复这些不变量，或发现 Host/Slot 所有权记录不一致，则不发布伪成功，直接进入 `FAULTED`。

无动画也不在导航调用栈中直接删除对象，而是在下一安全 Tick 走相同收尾路径。这样有动画和无动画不会产生两套生命周期。

## 8. 安全 Tick

`ui_runtime_tick()` 必须由 UI 循环在本轮 `lv_timer_handler()` 和输入事件分发返回之后调用，不能从 Adapter/observer 回调递归调用。

每个 Tick 的固定顺序为：

1. 锁存动画完成、Watchdog、Transient deadline、suspend/resume 请求和 fault；fault 一旦存在，本 Tick 只发布 FAULTED，不再进入下面任何会触碰受管树或 Adapter 的步骤；
2. 完成当前导航的成功或失败收尾；
3. 清理 Overlay 的退休 Slot，并至多激活每个 lane 的一个后继项；
4. 执行已请求的非活动页面 ViewSlot 回收；
5. 在仍将保持 RUNNING 时，合并 render 当前 Entry 的 dirty 状态；
6. 完成 suspend/resume 边迁移和焦点交接；
7. 最后发布本 Tick 形成的 observer 事件。

每 Tick 最多删除 `UI_FINALIZE_BATCH_MAX` 棵根 Slot。默认可以等于全部编译期容量以保持实现简单；只有实测 reset/pop_to 删除超过帧预算后才降低批量。

回调期间除 `*_is_valid` 和只读查询外，直接调用 Runtime 修改方法返回 `UI_ERR_REENTRANT`。需要后续动作时通过产品事件循环投递到下一 Tick。

## 9. 生命周期

典型新页面：

```text
create_state
    -> RootHost.create_slot
    -> mount_view
    -> on_activate(NEW)
    -> render
    -> [VISIBLE]
    -> on_deactivate(COVERED 或 REMOVED)
    -> hide
       或 unmount_view -> RootHost.delete_slot
    -> destroy_state（仅当 Entry 退出栈）
```

从栈内恢复且 View 已回收：

```text
existing state
    -> RootHost.create_slot
    -> mount_view
    -> on_activate(BACK/REENTER)
    -> render
```

关键约束：

- `on_deactivate` 只表示页面失去逻辑前台，不等于取消网络、OTA、相机或音频任务；
- Adapter 应在 deactivate 时暂停只服务于前台 View 的动画/timer，在 unmount 前彻底解绑所有可能访问 Slot 子树的回调；
- View 局部状态由 Adapter 在 deactivate 或 unmount 中保存，Runtime 不提供独立 `save_view_state` 回调；
- 如果局部状态必须覆盖“未发生 deactivate 就被显式回收”的路径，应在 unmount 中保存；因此 unmount 是最终兜底，deactivate 只用于尽早暂停前台行为；
- 隐藏 Entry 的 invalidate 只设置 dirty；下次恢复前合并 render 一次；
- 每次成功 mount 恰好对应一次 unmount；mount 失败不调用 unmount；
- 每个 Entry state 恰好销毁一次，并严格晚于其最后一个 Slot 删除返回；
- Runtime suspend 不销毁 Entry 或 Slot，也不触发 Entry-owner Overlay 的 owner-lost；
- 意外删除受管 Slot 时不再调用任何 Adapter 生命周期，直接进入 fail-stop。

## 10. 内存模型

Runtime 核心使用应用提供的固定存储：

- `UI_RUNTIME_MAX_DEPTH + 1` 个 Entry 槽位；
- 固定 Overlay 槽位和一个 Transient latest-wins 原子交换 sidecar；
- 一个导航事务、一个 deadline、三个 lane 状态；
- dirty/pending bit、focus owner 和 suspend/fault 状态。

核心不使用动态容器扩容。页面/Overlay ViewSlot、LVGL 子对象、style/event/group/animation 元数据仍由 LVGL allocator 提供，opaque state 和业务资源由 Adapter/业务模块负责。

正式端口必须明确目标 LVGL allocator 的失败模式。如果对象创建可以返回 NULL，RootHost/Adapter 把 Slot 或子树构造失败映射为 `UI_ERR_VIEW_MOUNT` 并走准备回滚；如果当前 LVGL 配置在 OOM 时只能 assert/abort，就不能在设计层虚构可恢复错误，产品必须通过静态预算、水位监控和系统级重启保证。`UI_ERR_HOST` 只表示 Host 层级/转场机制错误，不用于普通 View 内存不足。

在 32 位目标、`MAX_DEPTH=16`、Overlay 实例上限 8 时，核心元数据仍以约 1--2 KiB 为目标；必须由正式实现的 `sizeof`、map 文件和编译期断言验证，不能把设计估算当作验收值。M3 页面转场使用一个固定 RootHost 状态和两个固定完成回调 sidecar，不在 Runtime 堆分配动画上下文；当前实测为 32 位编译模型 1,836 字节、64 位测试构建 2,584 字节，分别低于公开存储的 2,048/3,072 字节。动画对象本身由 LVGL 管理，页面/业务资源不计入 Runtime 元数据。

### 10.1 View 回收

Runtime 不广播通用 `on_trim(level)`。它只提供与自身所有权直接相关的两种机制：

```text
UI_RECLAIM_ONE_OLDEST   回收一个最久未使用的非活动 RETAIN Slot
UI_RECLAIM_ALL_HIDDEN  回收全部非活动 Slot
```

请求在安全 Tick 合并执行；`ALL_HIDDEN` 覆盖 `ONE_OLDEST`。受害 Entry 依次执行 `unmount(RECLAIM) -> RootHost.delete_slot`，state 和 handle 仍留在返回栈中，下一次返回时重新 mount。当前栈顶页面即使处于 SUSPENDED 也不作为受害者。

reclaim 可以在导航 BUSY 或 SUSPENDED 时请求，只设置一个合并后的 pending mode；安全 Tick 会在当前导航收尾后执行。没有合适受害者时返回/完成为 no-change，不把它当作内存错误。FAULTED 时拒绝请求。

平台发生内存压力时，可以同时通知 Runtime 回收隐藏 View，并直接通知图片缓存、解码器、相机等资源模块；后者不经过 PageManager。已经保留的隐藏 Slot 随时可以通过回收接口释放，这就是 `RETAIN` 策略的动态清理能力。

### 10.2 RAM 峰值

保守峰值包括：

```text
所有隐藏 RETAIN Slot/子树
+ 转场旧/新 Slot/子树
+ StatusBar/Overlay Slot/子树
+ Entry/Overlay state
+ LVGL allocator 峰值
+ 业务图片、canvas、解码和 DMA 资源
```

两个页面对象树同时存在不会自动复制 display framebuffer 或 LVGL draw buffer。真正可能双份的是对象/style/event/group 元数据，以及 Adapter 自己创建的 canvas、bitmap 或私有 buffer。

即使关闭动画，为了保证“目标准备失败时旧页面不消失”，准备阶段仍可能同时持有旧/新两个 ViewSlot。若产品连这个峰值都无法承担，只能把页面和大资源进一步拆开复用，或者选择不可回滚的低内存切换；后者不属于本稳定基线。

## 11. OverlayManager

Overlay descriptor 同样在 Runtime 启动前注册并保持只读，不要求排序；至少包含：`overlay_type`、lane、Notice 是否允许 pointer 交互、Back 策略、默认 Transient 时长、Adapter 和 `user_ctx`。Modal 固定可交互，Transient 固定非交互；运行时 request 提供动态 `key`、priority、owner 和 args。

对于类型未知但内容动态的场景，注册一个通用 descriptor，由它在 args 中解析动态标题、正文、按钮和模型引用；动态的是实例数据，不是运行期间修改 descriptor 行为。

```text
视觉层级：Page < Notice < Modal < Transient
```

视觉层级不等于输入优先级：

- Modal 存在时独占 keypad/encoder 和页面输入，蒙版覆盖整个 RootScreen，包括状态栏；
- Notice 位于 Modal 蒙版下方，因此有 Modal 时其 pointer 自然被遮挡；Transient 虽位于 Modal 上方但固定 pointer-transparent，不能抢输入；
- 没有 Modal 时 Notice 可以按 descriptor 接收 pointer；需要 keypad/encoder 焦点的交互应建模为 Modal；
- 无 Modal 时页面拥有 keypad/encoder focus scope。

Modal 的 Back 策略只允许 `DISMISS` 或 `CONSUME`，两者都会阻止 Back 继续到页面；Notice/Transient 默认不参与 Back。priority 只在同一 lane 内比较，绝不改变三个 lane 的固定视觉顺序。

三个 lane 的规则：

- `Transient`：容量基线为 1，latest-wins，使用单 deadline 自动消失；deadline 与转场一样使用单调 `uint32_t` 时间和回绕安全比较；
- `Notice`、`Modal`：priority 降序、FIFO 稳定排序，高优先级可以抢占当前项；
- Notice/Modal 容量统计所有 LIVE 和等待安全删除的 RETIRED 实例；满时返回 `UI_ERR_CAPACITY_FULL`，不隐式删除已有实例，也不提前复用退休槽位。Transient 的逻辑容量固定为 1，但另有一个不计入逻辑容量的候选 sidecar，只用于 latest-wins 原子交换；
- 等待实例只保留 state，不创建 LVGL ViewSlot；每 lane 稳定态最多一个当前 Slot；
- 被抢占实例执行 deactivate/unmount 并保留 state，重新成为当前项时再 mount；
- 同 `(overlay_type, key)` 再次 show 表示更新同一实例，成功后 handle/priority/owner/FIFO 不变；priority 和 owner 不允许借更新改变；
- Entry-owner Overlay 在页面因导航 deactivate 时逻辑关闭；Runtime-owner Overlay 不受页面导航影响；display suspend 只暂停显示，不使 owner 失效。

Overlay 根对象也使用 Runtime-owned ViewSlot。mount 失败遵循与页面相同的规则：Adapter 同步撤销非 LVGL 资源，RootHost 在安全 Tick 删除 Slot，不存在“交出部分根对象”或 `unmount(NULL)`。

Overlay Adapter 使用与页面相同的 state、mount/unmount、activate/deactivate 和 render 形状，另外提供 `update_state(args)` 和最终一次 `on_dismiss(reason)`。按钮或手势仍由 Adapter 的 LVGL event 回调解释，业务层拿 handle 调用 update/dismiss；Runtime 不再增加一个只做转发的 `on_action(action_id)`。`update_state` 必须先校验/复制 args，再一次性提交自己的 state；返回失败时旧 state/View 保持不变。当前项更新成功后只 render 原 Slot，等待项只更新 state，**不为了动态内容更新复制整棵 View**。

只有“新的实例将抢占当前项”才先 mount 候选 Slot，成功后再关闭旧 Slot 的输入并交换；失败时旧当前项不变。Notice/Modal 候选占用该 lane 尚未使用的正常容量，lane 已满时拒绝；容量固定为 1 的 Transient 使用全局唯一的额外 sidecar 完成 latest-wins。旧 Slot 在安全 Tick unmount/delete 前保持不可见。每 lane 因而稳定态最多一个 Slot，交换瞬间最多两个，不需要动态 finalizer 队列。

等待项被调度时若 mount 失败，该实例以 `ACTIVATION_FAILED` dismiss，并在当前 Tick 最多再尝试一个后继项，防止坏 descriptor 形成无限循环。dismiss、抢占或 owner 失效后的物理清理尚未完成时，该 lane 对新的外部结构操作返回 `UI_ERR_BUSY`；内部 fault/suspend 和 owner 失效仍能锁存处理。

页面转场期间允许 Runtime-owner Overlay 在自己的 lane 中 show/update/dismiss；它可以先显示，但转场输入闸门解除前不接收任何 pointer/keypad/encoder/button 输入。Entry-owner show 必须携带当前 ACTIVE Entry handle，提交后已经 inactive/失效的旧 handle 会被拒绝。SUSPENDED 时不接受 Overlay 变更。一个 lane 有抢占/dismiss/owner-lost 的 Slot 等待安全清理时，新实例 show 和 dismiss 返回 `UI_ERR_BUSY`；已经 LIVE 的实例仍允许 update state，以免业务结果被清理窗口阻塞。

业务任务、operation token、网络取消和业务超时属于业务模块；OverlayManager 只接受业务完成后的 update/dismiss。按钮事件由 Adapter 自己转换成这两个公共操作，因此 Runtime 不理解 action ID 或按钮语义。

最终关闭的顺序固定为：立即撤销输入、使 handle 失效并隐藏当前 Slot，随后同步执行轻量的 `on_deactivate(reason)` 以停止 View 活动；安全 Tick 中执行 `unmount -> RootHost.delete_slot -> on_dismiss(reason) -> destroy_state`。`on_deactivate` 期间 handle 已无效但借用 Slot 仍有效，Runtime 修改方法仍受回调重入保护；`on_dismiss` 只接收 state 和 reason，不接收 ViewSlot。等待队列中从未 mount 的实例直接在安全 Tick 执行 `on_dismiss -> destroy_state`。抢占不是最终关闭，只执行 deactivate/unmount/delete 并保留有效实例 state；后续重新调度时再次 mount/activate/render。

## 12. StatusBar

StatusBar 是 ShellHost 的长期子树，不是页面 Entry，也不是 Overlay：

- Runtime 初始化时，RootHost 创建一个固定 StatusBarSlot，StatusBarAdapter 在其中 mount 内容；
- ShellHost 激活时显示，FullscreenHost 激活时隐藏；Route 只提供有限样式枚举；
- RootHost 只负责 Slot 的布局、整体 show/hide 和样式通知；
- 电量、Wi-Fi、时间、蓝牙等模型、订阅、去重和 timer 全部属于 StatusBarAdapter；
- 应用直接更新 StatusBarAdapter，隐藏期间是立即更新隐藏 View 还是只合并快照，由 Adapter 自己决定；
- Runtime 不调用“同步业务数据”接口，也不等待状态栏数据，因此状态栏更新失败不会使导航 degraded；
- 如果没有 StatusBarAdapter，ShellHost 的 ContentHost 自动占满可用区域。

应用配置了 StatusBarAdapter 但其 mount 失败时，`ui_runtime_init()` 整体失败并按初始化规则回滚；运行中状态数据暂时不可用只影响栏内内容，不影响 Runtime。状态栏业务更新必须和其他 LVGL 更新一样，经 UiDispatcher 回到 UI 线程。

StatusBarAdapter 的最小机制接口为 `mount(slot)`、可选健康关闭使用的 `unmount(slot)`、`set_visible(visible, reason)` 和 `apply_style(style)`。RootHost 只在显隐/样式边发生变化时通知；这些回调均为 void，不能否决导航。状态栏高度是 Runtime config 的布局参数，不由页面返回；初始化时必须在 display 可用高度内。Adapter 可依据可见性自行暂停局部动画，但 Runtime 不要求或读取其业务快照。

健康关闭扩展可以先让 Adapter unmount，再由 RootHost 删除 StatusBarSlot。意外删除 Slot 则与页面一样进入 fail-stop，不调用 NULL 清理回调。

## 13. 焦点与输入

导航 BUSY、LVGL 输入闸门和物理输入去抖是三个不同问题：

| 问题 | 负责模块 | 规则 |
|---|---|---|
| 旧导航未完成又收到导航 | PageManager | 返回 `UI_ERR_BUSY`，不排队 |
| 转场中对象不应接收事件 | RootHost | 临时禁用命中和 focus scope，并吞掉当前按压尾事件 |
| 一次按键产生 repeat/长按/release | InputAdapter | 转成产品定义的一次或多次逻辑动作 |

Runtime 不提供 `ui_runtime_input_report()`，也不跟踪物理设备按下计数。事务开始时，RootHost 对配置给它的每个 LVGL indev 调用端口适配的 `input_quiesce()`：它在支持该能力的 LVGL 版本中映射到 wait-release，在不支持的版本中由 InputAdapter/端口层屏蔽当前 pointer/button 事件并合成 cancel。这样即使转场早于手指/按键释放完成，旧按压的 release/click 也不会命中新页面。InputAdapter 不应从 ISR 或裸按键回调直接反复调用导航；它先完成去抖和动作归一化，再通过 UiDispatcher 在 UI 线程发出逻辑命令。

`input_quiesce()` 不是物理去抖：设备断连或驱动丢失 release 时，InputAdapter 仍必须合成 cancel/release；产品允许自动 repeat 时，也必须保证每次逻辑动作只提交一次导航请求。若某输入设备没有交给 Runtime，调用方负责确保它不会绕过转场闸门操作受管页面。端口必须在初始化时声明 quiesce 能力；若既不能 wait-release 也不能屏蔽事件，则该 indev 不得注册为 Runtime-managed。

### Focus scope

- 每个 mounted 页面或 Modal Slot 可以按需拥有一个 RootHost 管理的 `lv_group_t` focus scope；
- Adapter 可以向自己的 scope 注册对象和声明初始焦点，但不能调用 `lv_indev_set_group()` 修改全局绑定；
- RootHost 在转场期间将配置的 keypad/encoder indev 绑定到空 scope；
- 转场完成后，有 Modal 则绑定 Modal scope，否则绑定活动页面 scope；
- Modal 成功激活后立即接管 scope；关闭后恢复页面原 scope 和其已有焦点；Slot 被重建时由 Adapter 重新声明初始焦点；
- suspend/fault 时解绑所有 scope；resume 时按同一优先级恢复；
- pointer hit-test 通过 Host/Slot hidden 和 event flags 控制，不靠 group 推断。

RootHost 只接管 `ui_runtime_config_t` 中显式登记的 indev，不扫描应用的其他设备。对 keypad/encoder，它管理 group；对 pointer/button，它只使用 wait-release 和 Host hit-test，不绑定 group。

RootHost 在 RootScreen 初始化时创建一个透明、默认隐藏的全屏 `InputShield`，其层级高于三个 Overlay lane。导航、suspend pending 和 fault 锁存时先启用 Shield，再处理任何生命周期；稳定 RUNNING 时隐藏 Shield。它只吸收 pointer/button，不显示业务 loading，也不解释按键。keypad/encoder 同期绑定空 group。由此输入闸门是固定 O(1) 操作，不需要递归开关整棵页面树。

## 14. Suspend、fault 与可选关闭

### 14.1 Display suspend/resume

灭屏不是 Runtime stop。Runtime 提供 `ui_runtime_suspend()` / `ui_runtime_resume()`，内部只保存一个期望显示状态并在安全 Tick 合并：

- suspend 请求立即关闭页面输入并把 Runtime 标记为 `SUSPEND_PENDING`；从这一刻起新的结构操作就返回 `UI_ERR_SUSPENDED`。若导航正在转场，安全 Tick 先吸附到已提交目标并完成事务，再进入 `SUSPENDED`；
- 活动页面、每个已激活 Overlay 和可见 StatusBar 分别收到 `on_deactivate(DISPLAY_SUSPEND)` 或等价的 visibility 通知，停止 View 动画，focus scope 解绑；
- suspend 本身不销毁返回栈、Entry state、页面/Overlay Slot 和状态栏 Slot；显式 `ui_runtime_reclaim_views()` 仍可回收隐藏页面 Slot，Entry/state 继续保留；
- suspend 不关闭 Entry-owner Overlay，不重置页面业务，也不停止相机、网络或音频任务；
- SUSPENDED 时结构导航和 Overlay 变更返回 `UI_ERR_SUSPENDED`，invalidate 只合并 dirty；
- resume 时先恢复 Host 可见性，调用页面/当前 Overlay 的 `on_activate(DISPLAY_RESUME)`，再 render 最新模型并消费 dirty，最后恢复 focus 和输入；任何对象都不能在 activate 前收到输入；
- Transient deadline 使用单调时间，默认在 suspend 期间继续流逝，过期项不会在亮屏后重新出现。

连续 suspend/resume 请求只保留最后期望状态；如果安全 Tick 前又恢复为 RUNNING，则取消 `SUSPEND_PENDING`，没有导航 BUSY 时按“Modal 优先、否则页面”的规则恢复正常输入，不制造一次无意义的生命周期往返。对已经进入 SUSPENDED 的 Runtime，resume 只进入 `RESUME_PENDING`，真正的 View/生命周期/focus 恢复仍在安全 Tick 完成。

为了休眠而主动吸附尚未结束的页面转场是正常控制流：该导航完成资源收尾后发布普通 `COMPLETED`，随后发布 `UI_RUNTIME_SUSPENDED`，不能因此标记 degraded。已经锁存的真实动画超时/Host 故障仍按原错误结果处理。SUSPENDED 期间必须继续调用 `ui_runtime_tick()`，以处理 resume、Transient 到期、回收请求和 fault；除 resume、重复 suspend、invalidate、reclaim 和只读查询外，公共修改方法返回 `UI_ERR_SUSPENDED`。

### 14.2 意外删除采用 fail-stop

RootHost 为 RootScreen、固定 Host 和每个受管 Slot 注册 `LV_EVENT_DELETE` 守卫。正常删除先设置内部标志；没有标志的删除表示所有权契约已经被破坏。父对象被非法删除可能连续触发多个子对象 DELETE；只锁存第一个 fault，后续守卫只清指针，不重复发布事件。

处理规则：

1. Delete 回调只清除对应受管根指针并锁存 fatal fault，不继续遍历对象树；Adapter 自己管理的普通子对象可以正常增删，只有 Runtime-owned 根对象受此守卫；
2. 立即关闭输入、解绑 focus，并使全部公开 Entry/Overlay handle 失效；
3. 下一安全 Tick 发布一次 `UI_RUNTIME_FAULTED`；
4. 不再调用 Page/Overlay/StatusBar Adapter，不尝试 best-effort unmount，不在可能损坏的树上继续导航；
5. 产品 fault handler 负责 blank display、重建整个隔离的 LVGL arena，或触发 watchdog/device restart。

这是故障隔离而不是内存泄漏策略。进入 FAULTED 后本 Runtime 不可恢复；故障前占用的对象/state 由更高层整体重启时释放。Debug 构建应在捕获非法删除时立即断言，以尽早发现所有权违规。

FAULTED 后仅允许 `ui_runtime_tick()` 完成一次事件交付，以及 generation/状态/handle 有效性等只读查询；其他方法统一返回 `UI_ERR_FAULTED`。产品应通过显示驱动或硬件层熄屏，不能要求故障 Runtime 再操作已经失去所有权一致性的 LVGL 树。

### 14.3 stop/reinit 不是核心能力

典型嵌入式产品中 UiRuntime 与 display/LVGL arena 同寿命，首版核心不提供通用 stop/reinit，也不要求应用长期保留 fallback screen、持久 generation header 或跨固件私有布局兼容。

确有“健康地卸载一个 display Runtime”需求时，可以增加独立、可编译关闭的 shutdown 扩展：只允许从非 FAULTED 状态进入，加载调用方提供的 fallback 后按正常 unmount/delete 顺序释放。该扩展不能用于意外删除后的恢复，也不能改变核心页面/Overlay Adapter 接口。

## 15. 线程、invalidate 与 observer

所有 Runtime/LVGL 方法和 Adapter 回调只在 UI 线程执行。PageManager 不内置跨线程消息队列。

- 非 UI 线程先更新自己拥有的线程安全模型，再通过固定容量 `UiDispatcher` 投递 UI 命令；
- 高频数据在业务模型或 Adapter 侧合并，不能塞满导航命令队列；
- Dispatcher 负责 payload 所有权、队列满策略、下一 Tick 投递和 Runtime epoch；
- Runtime 为每个 Entry 只保存一个 dirty bit，重复 invalidate 自动合并；
- 活动且 mounted 的 Entry 在下一安全 Tick render；隐藏 Entry 只保留 dirty；重建 mount 后 render 最新模型；
- generation 只判断页面/Overlay 实例是否仍有效，不能代替业务 operation revision。

UI 线程身份必须由平台配置的 `is_ui_thread(ctx)` 判定；首版实现不依赖不可移植的线程 ID 捕获。初始化及多个 display Runtime 的初始化也必须由产品串行化，不能把“调用方自觉”作为唯一保护。除非未来明确增加原子快照接口，当前所有查询也只允许 UI 线程调用。ISR 和非 UI 线程只能写业务模型或向 UiDispatcher 投递，不能调用 Runtime，也不能直接操作受管 LVGL 对象。

Runtime 初始化时注册一个全局 observer；单次导航不携带回调。事件至少包括：

```text
UI_NAV_STARTED
UI_NAV_FAILED
UI_NAV_COMPLETED
UI_NAV_COMPLETED_DEGRADED
UI_RUNTIME_SUSPENDED
UI_RUNTIME_RESUMED
UI_RUNTIME_FAULTED
```

`STARTED` 在导航调用内同步交付；`FAILED/COMPLETED*` 只从后续安全 Tick 交付。observer 不得阻塞或递归修改 Runtime，需要动作时投递到下一 Tick。

每个通过准入并开始准备的导航获得非零 `txn_id`，最终恰有一个 `FAILED/COMPLETED*`。同步准入拒绝和 `SINGLE_TOP` reenter 的 `txn_id` 为 0。fault 直接终结所有在途事务，observer 收到 `UI_RUNTIME_FAULTED` 后必须丢弃它们。

## 16. 公共接口草案

下面只表达公共调用面的设计形状；已落地的字段顺序、枚举值和 `abi_version/struct_size` 定义以 `ui_runtime/include/ui_runtime/ui_runtime.h` 为唯一权威来源。RootHost 内部操作表不公开。

```c
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <lvgl.h>

#if LVGL_VERSION_MAJOR >= 9
typedef lv_display_t ui_lv_display_t;
#else
typedef lv_disp_t ui_lv_display_t;
#endif

typedef struct ui_route_desc ui_route_desc_t;
typedef struct ui_overlay_desc ui_overlay_desc_t;
typedef struct ui_status_bar_adapter ui_status_bar_adapter_t;
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

typedef enum {
    UI_RUNTIME_STATE_UNINITIALIZED,
    UI_RUNTIME_STATE_STARTING,
    UI_RUNTIME_STATE_RUNNING,
    UI_RUNTIME_STATE_SUSPEND_PENDING,
    UI_RUNTIME_STATE_SUSPENDED,
    UI_RUNTIME_STATE_RESUME_PENDING,
    UI_RUNTIME_STATE_FAULTED,
} ui_runtime_state_t;

typedef struct {
    ui_result_code_t code;
    uint32_t txn_id;      /* 仅页面导航事务可以非零 */
} ui_result_t;

typedef enum {
    UI_RECLAIM_ONE_OLDEST,
    UI_RECLAIM_ALL_HIDDEN,
} ui_reclaim_mode_t;

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
    UI_NAV_STARTED,
    UI_NAV_FAILED,
    UI_NAV_COMPLETED,
    UI_NAV_COMPLETED_DEGRADED,
    UI_RUNTIME_SUSPENDED,
    UI_RUNTIME_RESUMED,
    UI_RUNTIME_FAULTED,
} ui_runtime_event_kind_t;

typedef enum {
    UI_EVENT_DETAIL_NONE               = 0,
    UI_EVENT_DETAIL_TRANSITION_TIMEOUT = 1u << 0,
    UI_EVENT_DETAIL_HOST_SNAP          = 1u << 1,
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

typedef void (*ui_runtime_observer_fn)(void *ctx,
                                       const ui_runtime_event_t *event);
typedef bool (*ui_runtime_thread_check_fn)(void *ctx);

typedef union {
    max_align_t align;
    uint8_t bytes[UI_RUNTIME_STORAGE_SIZE];
} ui_runtime_t;

typedef struct {
    const void *data;
    uint16_t size;
} ui_args_t;

typedef enum {
    UI_OVERLAY_OWNER_RUNTIME,
    UI_OVERLAY_OWNER_ENTRY,
} ui_overlay_owner_kind_t;

struct ui_overlay_request {
    uint32_t key;             /* 在一个 overlay_type 内唯一 */
    int16_t priority;         /* 实例创建后不可通过 update 改变 */
    ui_overlay_owner_kind_t owner_kind;
    ui_entry_handle_t owner;  /* ENTRY owner 必填 */
    uint16_t timeout_ms;       /* 仅 Transient；0 使用 descriptor 默认值 */
    ui_args_t args;           /* 只在当前调用期间有效 */
};

typedef struct {
    const ui_route_desc_t *routes;
    uint16_t route_count;
    const ui_overlay_desc_t *overlays;
    uint16_t overlay_type_count;
    uint16_t initial_route_id;
    ui_args_t initial_args;
    uint16_t max_depth;
    uint16_t modal_capacity;
    uint16_t notice_capacity;
    uint16_t transient_capacity; /* 基线为 1 */
    ui_lv_display_t *display;
    lv_indev_t *const *indevs;  /* Runtime 接管的 pointer/keypad/encoder/button */
    uint8_t indev_count;
    const ui_status_bar_adapter_t *status_bar; /* 可为空 */
    void *status_bar_ctx;
    lv_coord_t status_bar_height; /* status_bar 非空时必须 > 0 */
    ui_runtime_observer_fn observer;
    void *observer_ctx;
    ui_runtime_thread_check_fn is_ui_thread; /* 首版必填 */
    void *thread_ctx;
} ui_runtime_config_t;

ui_result_t ui_runtime_init(ui_runtime_t *, const ui_runtime_config_t *);
void ui_runtime_tick(ui_runtime_t *, uint32_t now_ms);
uint32_t ui_runtime_generation(const ui_runtime_t *);
ui_runtime_state_t ui_runtime_get_state(const ui_runtime_t *);

ui_result_t ui_runtime_suspend(ui_runtime_t *);
ui_result_t ui_runtime_resume(ui_runtime_t *);

ui_result_t ui_nav_push(ui_runtime_t *, uint16_t route_id,
                        const ui_args_t *, ui_entry_handle_t *out);
ui_result_t ui_nav_pop(ui_runtime_t *);
ui_result_t ui_nav_replace(ui_runtime_t *, uint16_t route_id,
                           const ui_args_t *, ui_entry_handle_t *out);
ui_result_t ui_nav_reset(ui_runtime_t *, uint16_t route_id,
                         const ui_args_t *, ui_entry_handle_t *out);
ui_result_t ui_nav_pop_to(ui_runtime_t *, ui_entry_handle_t target);
ui_result_t ui_nav_back(ui_runtime_t *);

ui_result_t ui_entry_invalidate(ui_runtime_t *, ui_entry_handle_t);
ui_result_t ui_entry_get_top(const ui_runtime_t *, ui_entry_handle_t *out);
ui_result_t ui_entry_find_topmost(const ui_runtime_t *, uint16_t route_id,
                                  ui_entry_handle_t *out);

ui_result_t ui_overlay_show(ui_runtime_t *, uint16_t overlay_type,
                            const ui_overlay_request_t *,
                            ui_overlay_handle_t *out);
ui_result_t ui_overlay_update(ui_runtime_t *, ui_overlay_handle_t,
                              const ui_args_t *);
ui_result_t ui_overlay_dismiss(ui_runtime_t *, ui_overlay_handle_t);

ui_result_t ui_runtime_reclaim_views(ui_runtime_t *, ui_reclaim_mode_t);
bool ui_entry_is_valid(const ui_runtime_t *, ui_entry_handle_t);
bool ui_overlay_is_valid(const ui_runtime_t *, ui_overlay_handle_t);
```

`ui_runtime_t` 的真实布局保持私有，但存储由应用静态提供；实现使用 `_Static_assert` 验证大小和对齐，不通过隐藏堆分配伪装 opaque 对象。Runtime generation 可以由模块内的单调 boot-local 计数器分配；首版不承担跨设备重启或固件升级保存旧 handle 的语义。

所有带 `out` handle 的方法进入时先把输出清零，只有明确成功后才写入有效 handle。同步准入拒绝、查询、Overlay、reclaim、suspend/resume 和 `SINGLE_TOP` reenter 的 `txn_id` 都为 0；只有已经发布 `STARTED` 的页面导航返回非零值。准备失败时方法返回具体错误和该 `txn_id`，安全 Tick 清理后 observer 再发布 `FAILED`。成功提交返回 `UI_OK_TRANSITIONING`，终态由 observer 发布。同步结果不携带 detail flags，因为 V2 中所有可降级机制都发生在异步转场收尾；详细 bit 只属于终态事件。

`STARTED` 中新候选的 `to_handle_valid=false`；pop/pop_to 的已有目标可以有效。Entry 在提交点退出栈后立即失效，所以 `COMPLETED.from` 不能靠 Route ID 推断仍可访问。`COMPLETED_DEGRADED` 的 `code` 仍为 `UI_OK`，原因只使用 `TRANSITION_TIMEOUT/HOST_SNAP` bit；`FAILED` 携带准备错误。suspend/resume/fault 事件的 `txn_id=0`。

Entry/Overlay generation、Overlay FIFO sequence 和 `txn_id` 从 1 开始，0 保留为无效值。任何计数器到达最大值时都拒绝需要新身份的操作并返回 `UI_ERR_GENERATION_EXHAUSTED`，不能静默回绕后重新接受旧 handle。对于通常不会热重建的嵌入式 Runtime，这相当于故障寿命保护；产品重建设备/UI arena 后开始新 epoch。

初始化会复制容量、display 和最多 `UI_RUNTIME_MAX_INDEVS` 个 indev 指针；调用方传入的 config 结构和 indev 指针数组本身不需要常驻。Route/Overlay/StatusBar descriptor 及其 `user_ctx` 不复制，必须在 Runtime 整个有效期保持不变。所有 descriptor ID 必须唯一，运行时容量不能超过对应编译期上限，`1 <= max_depth <= UI_RUNTIME_MAX_DEPTH`。

`ui_args_t.size > 0` 时 `data` 必须非空；size 为 0 时 data 被忽略。Runtime 不保存该指针。配置中的 display 必须已由 LVGL 初始化；每个 indev 必须非空、去重并属于该 display，类型只允许 pointer、keypad、encoder 或 button。只有 keypad/encoder 参与 focus group。

`ui_entry_get_top()` 在 RUNNING 和 SUSPENDED 都返回栈顶有效 handle；仅 RUNNING 时它同时表示 ACTIVE Entry。`ui_entry_find_topmost(route)` 从栈顶向根查找第一个匹配实例。查询不暴露 Slot/state，也不延长 Entry 生命周期。

## 17. 对 unigui 业务的覆盖验证

| unigui 场景 | 新机制 | 业务仍由谁负责 |
|---|---|---|
| Home、Settings、Wi-Fi、音量、亮度 | Shell Route + RETAIN/REBUILD | 各页面模型与控件 |
| Camera、Picture、Barcode、Keyboard | Fullscreen Route，必要时 Overlay | 相机、解码、键盘业务 |
| 列表、图片预览控制条、页面内步骤 | PageAdapter 内部状态 | 对应页面 |
| Message、Dialog、Forget Wi-Fi、Spinner | Modal/Notice descriptor | 文案、按钮动作、任务 |
| Tooltip、音量条、短反馈 | Transient latest-wins | 动态显示数据 |
| `page_manager_pop_to(CAMERA_PAGE)` | `pop_to(entry_handle)` 或 SINGLE_TASK | 流程决定仍在业务层 |
| 直接取得 page 指针再更新 | 业务模型 + `ui_entry_invalidate()` | 页面 render |
| `lv_status_bar_*` 更新 | 直接更新 StatusBarAdapter | 状态栏模块 |
| 相册、图片流、缩略图、DMA | Runtime 只回收 ViewSlot | 资源模块按预算复用/降级 |
| Camera/Wi-Fi 退出后仍需工作 | 页面仅解绑 View | 业务任务生命周期 |
| 页面 timer/动画 | activate/deactivate/unmount | PageAdapter |
| 键盘直接 `lv_group_focus_obj()` | ViewSlot focus scope | RootHost 绑定全局 indev |
| 灭屏时逐个重置业务页面 | Runtime suspend 保留 UI | 产品业务协调器按需求重置 |

覆盖这些场景不要求 Runtime 认识 Camera、Wi-Fi、Album、OTA 等类型。现有 unigui 中按页面类型决定是否删除、返回裸 `lv_obj_t *`、逐页面处理灭屏和页面直接修改全局 group 的做法，都应在迁移时拆回各自所有者。

迁移页面创建函数时，把 `lv_*_page_create(NULL)` 改为接收 `ui_view_slot_content(slot)`；不能继续把页面创建为独立 screen，也不能保存 Slot 根对象供全局查找。

### 17.1 从两个参考页面管理器吸收与拒绝的设计点

| 来源 | 吸收 | 不吸收 |
|---|---|---|
| `lvgl-pm` | 页面注册、返回历史、统一有限转场、父对象删除子树 | 全局裸 page 数组、同 Route 只能一个实例、六个外观阶段、把 Popup 当页面动画、动画全局变量和堆回调数据 |
| `lv_scr_mgr` | 静态 descriptor、create/destroy 配对、返回到根、测量页面内存峰值的工程意识 | 每次页面都是 screen、动态链表栈、无动画时临时空 screen、修改 LVGL 内部 `scr_anim_ready`、全局共享 param |
| Android UI | Entry 身份、Back 栈、先准备后提交、state/View 分离、singleTop/singleTask、suspend 思路 | Activity/Fragment 全生命周期、多返回栈、无限任务队列、通用业务 ViewModel/缓存框架 |

V2 中的 ViewSlot 与 `lvgl-pm` 的 page parent 外观相似，但所有权更严格：Slot 由 RootHost 为**页面实例**创建，而不是按 Route 注册一个全局 page；Adapter 从来不持有或删除这个根。内存测量也只作为产品诊断，不进入页面切换逻辑。

## 18. V2 与旧基线比较

| 维度 | 旧基线 | V2 精简基线 | 结果 |
|---|---|---|---|
| 根 View 所有权 | Adapter 创建并在失败时交出部分根 | RootHost 先创建 ViewSlot | 失败路径更容易证明 |
| 生命周期 | pause/resume/reenter/save/render phase/trim/before-destroy | mount/unmount/activate/deactivate/render/back | 回调和顺序约束减少 |
| mount 失败 | 允许 `before_view_destroy(NULL)` | Adapter 同步撤销非 LVGL 资源，RootHost 删除 Slot | 不再把故障指针传给页面 |
| 内存压力 | Runtime 广播业务 `on_trim` | Runtime 只回收自己拥有的隐藏 Slot | 责任更纯粹 |
| 输入 | Runtime 跟踪 ACTIVE/IDLE 尾部 | BUSY + RootHost 闸门；去抖在 InputAdapter | 删除设备语义耦合 |
| 焦点 | 只有输入盾 | Slot focus scope + Modal 恢复 | 覆盖旋钮/键盘产品 |
| 灭屏 | 容易误用 stop | suspend/resume 保留全部结构 | 恢复快且不重建业务 |
| 状态栏 | Runtime 触发数据 sync 并影响 degraded | RootHost 只布局/显隐/样式 | 导航不依赖业务数据 |
| 意外删除 | best-effort 生命周期清理 | fail-stop，不触碰损坏树 | 避免二次 UAF |
| stop/reinit | 核心必选，含 fallback/持久头 | 独立可选 shutdown 扩展 | 首版明显减重 |
| RAM | 无 Slot 包装对象 | 每个 mounted 根多一个小对象/可选 group | 小幅增加换取确定所有权 |

V2 的核心能力没有减少：返回栈、失败回滚、RETAIN/REBUILD、Overlay 三 lane、全屏、状态栏、观察事件和延迟删除仍然保留。删掉的是不属于页面管理器的业务通知以及低价值异常恢复契约。

## 19. 实现顺序与生产门槛

推荐按以下顺序实现：

1. RootHost、ViewSlot 和固定 Entry 栈；先完成 push/pop/replace/reset/pop_to。
2. mount/unmount/activate/deactivate、RETAIN/REBUILD 和 mount 失败回滚。
3. RootHost 转场、txn observer、Watchdog 和固定批量安全收尾（已在 `ui_runtime/` 完成，并通过真实 LVGL 9.3 PC smoke）。
4. ViewSlot focus scope、Modal 输入仲裁和 suspend/resume。
5. Overlay 三 lane 及原子更新；随后接 StatusBarAdapter。
6. 用 unigui 的 Home、Settings、Wi-Fi、Dialog、Picture、Keyboard 六类页面迁移验证。
7. 实测每个 Route/Overlay 的稳定 RAM、准备峰值、mount 时间和单 Slot 删除时间，再确定策略和容量。

当前验证证据：

- 独立状态机在 Debug/Release 下共通过 42 项测试，并完成两组各 100,000 次随机动作验证；
- 通过 GCC `-fanalyzer`、LVGL 8/9 严格编译，核心元数据实测为 32 位 1,836 字节、64 位 2,584 字节；
- `lv_pc_a` 使用真实 LVGL 9.3 验证了动画 push/pop、BUSY 准入、状态栏显隐、Modal 输入独占和更新、focus scope 恢复、延迟删除顺序以及 suspend/resume；
- `lv_pc_a` 还直接复用了 unigui 的 Settings、Factory、Message、Spinner 和 StatusBar View，验证 Shell/Fullscreen、REBUILD、Modal/Transient、隐藏状态栏数据更新和应用回调导航；QOI 解码、资源映射和字体回退都留在验证宿主，没有进入 Runtime；
- 在 8 MiB PC LVGL heap、4 MiB 图片缓存上限下，上述 unigui smoke 的 allocator 历史峰值为 3,141,856 字节，结束时最大连续空闲块为 5,290,560 字节、碎片率 2%；该数据只作为 PC 样本；
- 三张 800x480 视觉回归已确认 QOI 资源非空且颜色正常、Modal 覆盖 StatusBar、Fullscreen 隐藏 StatusBar；Factory 的原字体子集缺字属于产品资源问题；
- PC 宿主已经验证 Runtime 默认启用、全部可选模块关闭、Runtime/MVP/MVU/unigui 全开等构建组合；
- 尚未完成的是目标板显示/触摸适配、LVGL allocator 峰值与碎片、实际 mount/删除时延和长期运行验证。

这次迁移刻意没有为旧接口制作假 `page_manager/popup_manager`：unigui Home 内建 StatusBar，Dialog 直接调用旧 PopupManager，Keyboard/Picture 直接调用旧 PageManager。它们需要先把 View 内的全局管理器调用改为回调或业务端口，应用 Adapter 再调用 Runtime。这个改动属于页面解耦，不是给 Runtime 增加页面类型或兼容分支。

生产实现至少必须验证：

- 目标 LVGL 版本中父子删除、`LV_EVENT_DELETE`、group 对象移除、动画取消和 display 绑定的实际顺序；
- mount 在每一个构造步骤失败时都不会遗留外部订阅，且部分 LVGL 子树能由 Slot 一次删除；
- 每次成功 mount 只有一次 unmount，state 只销毁一次且晚于 Slot 删除；
- 100,000 次以上随机 push/pop/reset/reenter/reclaim/Overlay/suspend 动作始终满足不变量；
- repeated key、long press、漏 release 和设备断连由 InputAdapter 正确归一化；
- suspend 发生在转场、Modal、Transient deadline 和大量 dirty 数据期间时仍可恢复；
- 非法删除任意 Page/Overlay/StatusBar Slot 后只发布一次 FAULTED，且 Runtime 不再调用 Adapter；
- 用 `sizeof`、map 和 LVGL allocator 水位验证核心元数据、稳定 RAM 和双 View 准备峰值。
- 内存验证同时记录 allocator 碎片率和最大连续空闲块；只有总空闲字节而没有最大块，不能证明下一页 Slot 能创建成功。

最终稳定态不变量：

1. RUNNING 稳定态返回栈中恰有一个 ACTIVE Entry，它是栈顶并拥有有效 mounted Slot；SUSPENDED 稳定态没有 ACTIVE Entry，但栈顶身份和所有 Entry/state 保留，隐藏 Slot 是否存在取决于显式 reclaim；
2. 其他栈内 Entry 只能拥有 HIDDEN Slot 或没有 Slot；
3. 每个 Slot 只有 RootHost 一个根所有者，每次成功 mount 恰好一次 unmount；
4. 每个 Overlay lane 稳定态最多一个 mounted 当前项；
5. 转场、suspend、fault 时没有页面或 Overlay focus owner；稳定运行时 Modal 优先于页面；
6. 所有删除只在安全 Tick，所有退出栈的 state 都晚于最后 Slot 删除；
7. Runtime 不保存业务图片、任务、状态栏字段、物理按键状态或无限命令队列；
8. FAULTED 后不再访问受管树，也不尝试局部恢复。

实现重量应保持在“一个固定栈事务状态机 + 一个 ViewSlot RootHost + 三个固定 Overlay lane”这一档。应用侧只需要学习 descriptor、handle、少量导航方法和精简生命周期；复杂的所有权、延迟删除、焦点交接和失败回滚都集中在 Runtime 内部。
