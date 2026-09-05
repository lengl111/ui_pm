# UI 页面管理与表现层交接

> **最终组合定案（冻结）**：产品级页面机制独立放在 `ui_runtime`；MVP/MVU
> 作为 Route 内可选的页面表现策略。对外可以用一个应用 Facade 统一调用，
> 但不得在 MVP/MVU 中再实现第二套产品级页面状态机。

## 交接目的

本文记录当前讨论中已经确认的架构结论，便于下一次会话或新项目继续
完善和验证。详细实现规范不在本文重复，优先参考已有设计文档。

## 权威资料

- `D:/xuexi/adr/ui/UI_PAGE_MANAGER_DESIGN.md`
- `D:/xuexi/adr/ui/ui_runtime/README.md`
- `D:/xuexi/adr/ui/ui_runtime/HANDOFF.md`
- `D:/xuexi/adr/ui/ui_runtime/include/ui_runtime/ui_runtime.h`
- `D:/xuexi/adr/mvp/docs/ui-architecture.md`
- `D:/xuexi/adr/mvp/docs/adr/0003-separate-page-mechanism-from-presentation.md`
- `D:/xuexi/adr/mvp/docs/mvp-implementation.md`
- `D:/xuexi/adr/mvp/docs/mvu-implementation.md`

如本文与公共头文件或现有测试不一致，以当前公共头文件和测试为准。

## 最重要的职责结论

一句话边界：

> `ui_runtime` 管理页面在哪里、何时存在以及何时可见；MVP/MVU 管理页面
> 显示什么、用户意图是什么以及需要向业务服务请求什么。

### `ui_runtime` 负责

- Route、Entry、返回栈和页面实例身份；
- 页面创建、挂载、激活、失活、卸载、销毁和重建；
- RootHost、ViewSlot、页面显示/隐藏和转场；
- Shell/Fullscreen 布局和状态栏整体显隐；
- Modal、Notice、Transient Overlay；
- 输入闸门、焦点域和页面级返回处理；
- suspend/resume、RETAIN/REBUILD、隐藏 View 回收和故障保护。

### MVP/MVU 负责

- 业务 Snapshot 的表现层投影；
- 页面草稿、pending、错误和请求关联状态；
- 用户意图的表达和校验；
- Command/Effect 的生成与投递协调；
- 将完整 ViewState/Model 投影到 LVGL 控件；
- 页面内部的 Panel、Mode、表单步骤或局部交互状态。

MVP/MVU 不拥有产品级页面栈，也不应该负责全局页面显隐、Overlay 仲裁或
状态栏整体管理。

### 其他模块负责

- RTAO/HRT/Services：真实系统数据、业务规则、设备操作和异步结果；
- LVGL Adapter：控件、布局、样式、绘制和局部控件事件；
- 资源模块：图片、解码、缓存、DMA buffer、相机流和大块媒体内存。

MVP/MVU 中保存的是表现层缓存或投影，真实业务数据的权威所有者仍是
RTAO/HRT/Services。

## 与 Android 的对应关系

```text
Android NavController / FragmentManager  ->  ui_runtime
Android ViewModel / Presenter / UI State  ->  MVP / MVU
Android View / Compose UI                 ->  LVGL View Adapter
Android Dialog                            ->  ui_runtime Overlay
```

因此：

- 简单单页或只有少量内部 Panel 的产品，可以直接使用 MVP/MVU + LVGL；
- 有产品级返回栈、弹窗、全屏页、统一生命周期、页面重建或内存回收时，
  使用 `ui_runtime`，它在职责上类似精简的 `NavController`；
- 无屏产品通常不引入 `ui_runtime`、MVP、MVU 或 LVGL。

是否使用 `ui_runtime` 不只取决于页面数量，而取决于是否需要通用页面机制。

## 推荐组合方式

```text
ui_runtime（一个产品级页面机制）
└── Entry / ViewSlot（唯一的页面根对象所有权）
    └── PageAdapter
        ├── 直接 LVGL 页面
        ├── MVP 页面
        └── MVU 页面
```

每个 Route 根据复杂度选择直接 LVGL、MVP 或 MVU；不要求整个产品统一使用
同一种表现模式，也不要求每个页面都套一层 MVP/MVU。

MVP 和 MVU 仍然是互斥的 Feature 表现策略：一个 Feature 选择其中一种，
不要在同一个 Feature 中同时使用两者。

## 当前组合方式中的问题

如果把现有 `MvuRuntime` 或 `MvpPresenter` 原样嵌入每个 `ui_runtime` Route，
会形成真实的同级职责重叠：两边都具有 activate/deactivate、bind/unbind、
render 和 hide 语义，MVU 示例的 `screen` 字段还可能与产品 Route 重复。

主要风险：

- 页面可见性出现两个控制者；
- 转场前后重复 hide/show，导致空白或闪烁；
- 同时启用 Feature 自动 render 和 PageAdapter render，造成重复投影；
- detach、销毁和回调重入顺序更难证明；
- 业务页面内部状态被误当成产品级导航状态。

这类嵌套方式可以作为旧代码的过渡方案或独立测试方式，但不应作为最终
产品集成形态。

## 最终推荐：宿主拥有页面生命周期

`ui_runtime` 是产品页面身份、生命周期和根对象显隐的唯一所有者。MVP/MVU
在集成模式下是页面内部的表现引擎：

- PageAdapter 在 `mount_view` 中创建控件并绑定 Feature；
- `on_activate` 只恢复 Feature 的订阅、动画或前台表现工作，不隐藏/显示
  Slot 根对象；
- `on_deactivate` 只暂停 Feature，不调用页面根对象的 `hide`；
- 页面内容渲染也只能有一个所有者：直接 LVGL 页面使用 PageAdapter 的
  `render + ui_entry_invalidate()`；现有 MVP/MVU 页面由 Presenter/MvuRuntime
  在 active 时自行 render，PageAdapter 的 `render` 必须留空；
- 隐藏期间收到的 Snapshot/Message 只更新 Feature 状态，不 render；重新激活
  时 MVP/MVU 执行一次完整 render，恢复最新内容；
- MVU 的 `screen` 只用于页面内部 Panel/Mode/向导步骤，产品级页面切换
  使用 `ui_nav_push/pop/back`；
- Feature Adapter 不得创建 LVGL screen、删除 ViewSlot 根对象、管理产品
  返回栈或管理全局状态栏/Overlay。

这里禁止的是操作 Runtime 拥有的 ViewSlot 根对象。Feature 可以隐藏自己创建的
内容子树来表达“等待首个 Snapshot”“局部 Panel 不可见”等页面内部状态。

集成页面的生命周期形状：

```text
ui_runtime mount_view      -> 创建控件并绑定 Feature
ui_runtime on_activate     -> Feature activate，并完整投影最新状态
ui_runtime render          -> MVP/MVU 页面留空；直接 LVGL 页面按需实现
ui_runtime on_deactivate   -> Feature pause，不调用 hide
ui_runtime unmount_view    -> 解绑事件和 View
RootHost 删除 ViewSlot
ui_runtime destroy_state   -> 销毁 Feature state
```

现有 `MvuRuntime`、`MvpPresenter` 仍可保留，用于不接入 `ui_runtime` 的单页
产品、独立 Feature 测试或兼容旧代码；新产品集成时应提供宿主生命周期模式，
而不是再建立第二个产品级页面管理器。

### 当前落地状态

上述是已经确认的目标架构，不表示现有 LVGL Feature Adapter 已经自动具备
宿主模式。MVP/MVU 在 active 时自行 render 是正确的内容机制；需要适配的是
其 deactivate 不能隐藏 ViewSlot 页面，并且 PageAdapter 不得再配置第二个
render 路径。在 hosted View 选项或等价适配器完成前，不能把现有会自动 hide
根容器的 LVGL Adapter 原样嵌入 `ui_runtime` Route。

## 导航和数据流

```text
RTAO/HRT/Service
        -> Snapshot / Result
        -> MVP/MVU
        -> ViewState / Model
        -> LVGL Adapter

LVGL 用户事件
        -> MVP/MVU 用户意图
        -> Command / Effect
        -> RTAO/HRT/Service
```

页面跳转可以由页面事件或 Feature 产生导航意图，但实际的 push、pop、back、
reset 和 pop_to 必须由应用调用 `ui_runtime` 执行。Feature 的 Update、Presenter
或 Command/Effect 回调只产生导航意图；应用在当前 Feature 调用返回后处理，
必要时经 UI Dispatcher 投递到下一安全轮次，避免同步重入生命周期回调。

状态栏数据、电量、Wi-Fi、时间等属于应用或系统服务；`ui_runtime` 只负责
状态栏整体挂载、布局、显隐和有限样式，不读取或拥有这些字段。

## 内存和性能结论

- 当前 `ui_runtime` 默认元数据约为每个 display 2 KiB；页面 Feature state
  单独由应用提供；
- 嵌套 MVP/MVU Runtime 增加的 RAM 通常不大，但主要代价是重复生命周期、
  重复渲染和更复杂的销毁路径；
- 真正占用 RAM 的通常是 LVGL 控件树、图片和媒体 buffer；
- `REBUILD` 释放页面控件树但保留轻量 Feature state；
- `RETAIN` 保留控件树以换取快速返回；
- 页面管理器不管理图片缓存、相机流、DMA buffer 或业务任务。

## 后续实现重点

1. 在架构文档中明确宿主生命周期集成模式；
2. 增加一个 MVP PageAdapter 和一个 MVU PageAdapter 示例，确保每个页面
   只有一个生命周期所有者和一个渲染调度路径；
3. 增加测试，验证 Feature 的 `hide` 不会破坏 Runtime 转场，隐藏期间的
   状态更新会在重新激活后正确渲染；
4. 完成两条端到端模板：
   - `HRT + ui_runtime + MVP + LVGL`；
   - `RTAO + ui_runtime + MVU + LVGL`；
5. 保留无屏模板：`HRT + Services`、`RTAO + Domain AO/Workers`；
6. 用 unigui 继续验证通用页面场景，但不把相册、图片流、相机等业务规则
   塞回 `ui_runtime`。

## 建议使用的技能

- `codebase-design`：继续确定模块接口、适配器接缝和所有权；
- `tdd`：为页面生命周期、转场、隐藏更新、RETAIN/REBUILD 增加集成测试；
- `diagnosing-bugs`：仅在真实 LVGL 出现闪烁、焦点残留、重入或销毁故障时使用；
- `domain-modeling`：需要把上述职责边界升级为新的 ADR 或项目上下文时使用。
