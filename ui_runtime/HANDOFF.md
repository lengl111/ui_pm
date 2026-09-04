# ui_runtime 新项目接入技术交接

## 下一阶段目标

在一个新的 LVGL 嵌入式产品中直接复用独立模块 `ui_runtime`，快速建立：

- 多页面导航和确定性生命周期；
- 公共顶部状态栏与少量全屏页面；
- 页面保留、重建及内存压力回收；
- Modal、Notice、Transient 三类弹层；
- 可在 PC 和目标板上自动验证的页面交互骨架。

不要重新设计页面管理器，也不要复制 `lv_pc_a` 中的验证代码作为生产框架。先按现有公共接口接入最小页面集合，再逐页迁移业务 View。

## 权威资料

以下文件是事实来源，交接文档不重复其中的完整设计和 API 定义：

- 总体设计：`D:\xuexi\adr\ui\UI_PAGE_MANAGER_DESIGN.md`
- 模块契约、构建方式和容量说明：`D:\xuexi\adr\ui\ui_runtime\README.md`
- 唯一公共 API：`D:\xuexi\adr\ui\ui_runtime\include\ui_runtime\ui_runtime.h`
- 核心实现：
  - `D:\xuexi\adr\ui\ui_runtime\src\ui_runtime.c`
  - `D:\xuexi\adr\ui\ui_runtime\src\ui_root_host.c`
  - `D:\xuexi\adr\ui\ui_runtime\src\ui_overlay.c`
- 独立单元/状态机测试：`D:\xuexi\adr\ui\ui_runtime\tests\test_ui_runtime.c`
- 真实 LVGL 9 + unigui 接入样例：`D:\xuexi\adr\ui\lv_pc_a\unigui_runtime_demo.c`
- PC 验证说明和实测数据：`D:\xuexi\adr\ui\lv_pc_a\README.md`

若文档和公共头文件不一致，以 `ui_runtime.h` 和当前测试为准。

## 已确认的架构边界

核心原则是“管理衣架，不管理衣服”。`ui_runtime` 只提供机制：

- Route、返回栈和页面实例 Handle；
- State/View 生命周期编排；
- Shell/Fullscreen 宿主布局；
- 状态栏整体显隐和有限样式值传递；
- 页面转场、输入屏障及焦点域；
- Overlay 仲裁、所有权和关闭规则；
- suspend/resume、内存回收、故障保护和导航观察事件。

以下内容必须留在应用、Presenter/Model、设备服务或资源模块：

- 页面具体内容和业务状态；
- 时间、电量、Wi-Fi 等状态栏数据；
- 相机、相册、网络、文件和后台任务；
- 图片解码、缓存、DMA buffer 和大块媒体内存；
- Home 内部横向分页、列表滚动等页面内部交互；
- 业务跳转规则和错误文案。

依赖方向必须保持：

```text
应用/验证宿主 -> ui_runtime -> LVGL 公共接口
```

禁止让 `ui_runtime` include 应用页面、旧 `page_manager`、`popup_manager` 或平台业务 DTO。

## 当前实现能力

已经实现并验证：

- 固定容量 Entry 池和有界返回栈；
- `push`、`pop`、`replace`、`reset`、`pop_to`、`back`；
- `STANDARD`、`SINGLE_TOP`、`SINGLE_TASK`；
- `UI_VIEW_RETAIN` 和 `UI_VIEW_REBUILD`；
- 回收最老隐藏 View 或全部隐藏 View，保留 Entry state；
- Shell 页面、Fullscreen 页面和唯一 RootScreen；
- 无动画、四向 Slide、Fade、延迟、完成回调和 watchdog；
- 单导航事务：未完成时新结构导航返回 `UI_ERR_BUSY`，不排队；
- `STARTED/COMPLETED/COMPLETED_DEGRADED/FAILED` 全局观察事件；
- 页面生命周期：state 创建/销毁、View 挂载/卸载、激活/失活、render、back；
- StatusBarAdapter 的挂载、卸载、整体显隐和样式应用；
- Notice、Modal、Transient 三通道弹层；
- Overlay 优先级/FIFO、抢占恢复、同 key 更新、超时和 Entry 所有权；
- Modal 导航阻断和输入独占；
- keypad/encoder focus group 切换；
- display suspend/resume；
- UI 线程、回调重入和 Runtime 所有 LVGL Root 完整性保护；
- 带 generation 的 Entry/Overlay Handle，逻辑移除后立即失效。

Runtime 元数据不做堆分配。默认限制下，32 位实测约 1836 B、预留 2048 B；64 位测试宿主实测约 2584 B、预留 3072 B。LVGL 对象、页面 state 和图片资源不包含在这个数字中。

## 新项目最短接入路径

1. 将 `ui_runtime` 作为独立 CMake 子模块加入项目，链接 `ui_runtime::ui_runtime`。不要把源码复制进应用页面目录。
2. 确保 Runtime 与调用方使用完全一致的 `UI_RUNTIME_MAX_*`、`UI_RUNTIME_STORAGE_SIZE` 和 watchdog 编译宏。
3. 建立应用侧静态 Route 表。descriptor、adapter 和 `user_ctx` 在 `ui_runtime_init()` 前完成注册，并在 Runtime 整个生命周期保持只读和有效。
4. 先接入三个代表页面：一个 `RETAIN` 根 Home、一个 `REBUILD` 普通设置页、一个 `REBUILD` Fullscreen 页。
5. 每个页面的 Adapter 明确区分轻量 state 与 LVGL View。需要跨重建保留的数据放 state/应用模型，不放 LVGL 对象。
6. 实现一个全局 StatusBarAdapter。应用服务可以在状态栏隐藏时继续更新其数据；Runtime 不读取这些数据。
7. 先注册一个 Modal 和一个 Transient overlay，跑通确认框、Loading/Toast、返回键和导航阻断。
8. 用 `UI_RUNTIME_INITIALIZER` 初始化存储，填写 `ui_runtime_config_t` 后在 UI 线程调用 `ui_runtime_init()`。
9. 在每轮 `lv_timer_handler()` 和输入分发返回后调用 `ui_runtime_tick(runtime, now_ms)`。不要在 LVGL/Adapter 回调栈中调用 Tick。
10. 页面点击事件只向应用 Adapter 请求导航。对 `UI_ERR_BUSY` 做去抖式忽略；不要自行建立第二套导航队列。
11. 注册全局 observer，以 `txn_id` 配对导航开始和终态事件。不要给每次导航携带临时完成回调。
12. 完成最小闭环后，再按业务优先级逐页迁移；高内存页面优先使用 `REBUILD`，确有快速返回价值且体积可控时才使用 `RETAIN`。

## 生命周期接入规则

- `create_state` 成功后，最终必有一次 `destroy_state`。
- 成功的 `mount_view` 最终对应一次 `unmount_view`；挂载失败不调用 unmount，Adapter 自己回滚非 LVGL 副作用，Runtime 在安全 Tick 删除整个 Slot。
- Adapter 只能在 `ui_view_slot_content()` 返回的父对象下创建页面子树，不得删除、换父或缓存 Runtime Slot root。
- `ui_args_t` 只在当前调用期间借用；页面要长期使用时必须复制到自己的 state。
- `render` 应当是从应用状态到 View 的幂等同步，不在其中发起业务任务或结构导航。
- `on_deactivate` 停止只应在前台运行的订阅/动画；真正的业务数据仍由应用模型持有。
- `UI_UNMOUNT_RECLAIM` 后 Entry state 仍存在，返回页面时必须能由 state 重建 View。
- 页面内部先消费返回操作时返回 `UI_BACK_RESULT_CONSUMED`；否则 Runtime 执行栈返回。根页面返回未处理时由应用决定退出、关机或忽略。

## 推荐的初始策略

| 页面类型 | Host | View 策略 | 原因 |
|---|---|---|---|
| 根 Home/高频主页面 | Shell | RETAIN | 快速返回并保留内部滚动状态；仍需支持压力回收重建 |
| 普通设置/详情页 | Shell | REBUILD | 降低隐藏页面 RAM，占用由轻量 state 恢复 |
| 相机/预览/厂测 | Fullscreen | REBUILD | 独占画面且资源昂贵；媒体资源由业务层释放 |
| 临时确认框 | Modal | 按描述表创建 | 独占输入并阻止页面导航 |
| 通知 | Notice | 固定容量 + 优先级 | 同通道高优先级抢占，等优先级 FIFO |
| Loading/Toast | Transient | latest-wins + timeout | 避免短时提示积压播放 |

Route 表只描述固定机制；每次导航的参数、页面 state 和 Overlay args 可以动态变化。新 descriptor 不在 Runtime 启动后注册。

## 接入时必须处理的返回码

- `UI_OK_TRANSITIONING`：事务已启动，等待 observer 终态。
- `UI_OK_REENTERED` / `UI_OK_NO_CHANGE`：不是失败，不要等待不存在的事务完成事件。
- `UI_ERR_BUSY`：前一事务尚未安全收尾，直接忽略重复按键或由应用在新用户动作时重试。
- `UI_ERR_MODAL_ACTIVE`：Modal 正在拥有输入，先按其关闭规则处理。
- `UI_ERR_BACK_STACK_FULL`：通常说明业务层级设计异常；根流程应使用 reset/singleTask 回到根，而不是无限嵌套。
- `UI_ERR_FAULTED`：Runtime 所有权约束已被破坏；当前实例不可继续使用，应记录诊断并走产品级恢复策略。

## 现有真实业务验证

`lv_pc_a` 只是验证宿主，不是生产实现。目前已经用原始 unigui View 跑通：

- 四屏 Home 横向拖动、分页指示器和图标懒加载；
- Home 作为 RETAIN 根页面，Home -> Settings -> Back 保留页码；
- 内存压力回收隐藏 Home View，重建后从 state 恢复语义页码；
- Settings、Wi-Fi、移动网络、显示、音量、存储、关于/关于本机；
- 多级返回栈、Fullscreen Factory、Modal Message、Transient Spinner；
- 唯一外部状态栏及隐藏状态下的数据更新；
- Wi-Fi 异步完成、转场期间重复请求拒绝、suspend/resume。

验证结果：

- 独立 Fake LVGL 测试包含 100,000 次页面状态操作和 100,000 次混合 Overlay 操作；
- 真实 LVGL 9 CTest 为 2/2 通过；
- GCC `-fanalyzer` 通过；
- 真实 SDL 鼠标拖动可从 Home 第 1 屏切到第 2 屏；
- 四屏 Home 和其余业务截图均为非空 800x480；
- 访问四屏 Home 并完成业务 smoke 的峰值约 3.24 MiB，主要是图片缓存；清缓存后活动 UI/Runtime 约 32 KiB。

复现命令见 `lv_pc_a/README.md`。不要把 PC 图片缓存数字当作目标板容量承诺。

## 尚未覆盖或需要新项目补齐

- 当前公共 API 没有 Runtime deinit；现有模型假定 Runtime 与产品 UI 宿主同寿命。若新项目必须动态销毁/重建整个 UI，先单独设计并补测试，不要私自删除 RootScreen。
- 不提供跨线程消息队列。设备线程必须通过项目已有 dispatcher 将动作投递到 UI 线程。
- 不保存掉电状态，也不序列化页面栈；持久化属于应用。
- 不内建 Tab/ViewPager/首页轮播；这是页面内部组件，Home 示例已证明可以共存。
- 不管理页面业务结果回传；共享 Model、命令端口或应用事件负责数据流。
- 不管理图片缓存和大块媒体 buffer。
- 当前只验证一个 display 的单 Runtime 用法。
- LVGL 8 在 `LV_USE_USER_DATA=0` 时只能使用无动画 Route，详细限制见模块 README。
- 相册、连续图片流、相机预览和目标板真实触摸尚未形成完整压力基线。
- 尚未完成目标板上的长时间随机操作、分配失败注入、休眠唤醒循环和低内存压测。

## 新项目验收清单

- 初始 Route 加载后生命周期次数和顺序正确。
- 普通 push/pop、singleTop、singleTask、reset 和根 back 正确。
- 快速连续点击不会重复入栈；事务结束后下一次点击仍能工作。
- Shell/Fullscreen 切换时状态栏只存在一份且显隐正确。
- RETAIN 页面快速返回保持状态；压力回收后重建仍恢复状态。
- REBUILD 页面隐藏后 LVGL 子树释放，Entry state 保留到出栈。
- Modal 阻止导航并正确处理 Back；Transient 不抢输入且能超时替换。
- keypad/encoder/mouse/touch 的焦点和输入域无残留。
- suspend/resume 后页面和 Overlay 状态一致。
- 页面创建失败、图片缺失和内存不足不会破坏当前可用页面。
- 长时间随机导航中 Entry、Overlay、LVGL object 和业务资源数量有界。
- 目标板记录静态 RAM、典型 RAM、峰值 RAM、最大连续空闲块和碎片率。

## 工作区注意事项

- `D:\xuexi\adr\ui` 当前不是 Git 仓库，不要假定存在可用的 commit/diff；修改前直接检查文件现状。
- 当前根目录没有 `.codegraph/`，除非之后建立索引，否则按仓库说明跳过 CodeGraph。
- 页面管理器生产实现只能保留在 `ui_runtime/`。`lv_pc_a/` 仅用于 PC + LVGL + 业务 View 验证。
- 最近为 Home 接入修改了：
  - `ref/unigui/src/lv_home_page.c/.h`
  - `lv_pc_a/unigui_runtime_demo.c`
  - `lv_pc_a/unigui_compat/lv_demo_common_compat.c`
  - `lv_pc_a/CMakeLists.txt`
  - `lv_pc_a/README.md`
- 用户偏好：少提零散问题；遇到复杂选择先内部比较 2-3 个方案，采用最简洁、稳定、边界清晰的方案，再说明关键取舍。

## 建议下一步

1. 在目标新项目中识别现有 LVGL 初始化、UI 线程、display、输入设备和 CMake target。
2. 建立应用自己的 `app_ui_runtime.c`、`app_ui_routes.c` 和页面 Adapter 目录；命名按新项目现有规范调整。
3. 用 Home + Settings + Fullscreen + Modal 做第一个可运行闭环。
4. 加入与 `lv_pc_a/unigui_runtime_demo.c` 同等级的集成 smoke，但断言新项目自己的生命周期和资源所有权。
5. 再迁移高资源业务，并在目标板建立内存/性能基线。

## Suggested Skills

下一位 Agent 应通过 Skill tool 使用：

- `codebase-design`：先确定新项目中 Runtime、应用导航 Adapter、业务 Model 和资源模块的边界。
- `tdd`：为首批 Route、生命周期、Back、Overlay 和低内存重建建立集成测试。
- `diagnosing-bugs`：仅在目标板出现黑屏、悬空焦点、转场卡住或内存回归时使用。
- `domain-modeling`：仅当新项目需要把页面/业务术语和所有权决策沉淀为 ADR 或 CONTEXT.md 时使用。

