# Paged List

`paged_list` 是用于大量索引型数据的固定窗口列表。它只保留当前页的
`visible_rows` 个 LVGL 行对象，不创建与数据总数等长的对象树或滚动高度。
适用于文件、录音、音乐、历史记录、告警和扫描结果等总数很大、数据由异步
服务提供的场景。

模块要求 **LVGL 9.5 或更高版本**。公共头文件包含编译期版本检查，不提供
LVGL 8 或早期 LVGL 9 的兼容分支。

它不属于 `ui_runtime`：`ui_runtime` 管 Route 和页面生命周期；`paged_list`
只管理一个页面内部的列表视口、焦点与行对象复用。文件系统、排序、缩略图、
播放、删除、网络/串口请求和业务缓存全部在调用方的数据服务中。
它也不抽象“相册/文件/媒体集合”等虚拟业务层；不同产品直接用自己的数据
服务实现下面几个小回调即可。

## 从参考工程吸收的点

`E:\jieli\luyinbi\recorder\release\deli_v2\uniapp\ui` 的实现中，以下思路
有复用价值：按索引访问、有限条目缓存、焦点变化时请求邻近数据、行模板复用。

原实现中不继承的部分：全局单例文件模型、同步等待 SD 卡、文件/播放业务耦合、
循环列表默认行为，以及用 `total_count * row_height` 构造 LVGL 长滚动内容。
最后一点在小坐标范围的嵌入式 LVGL 配置中会溢出，也会让大列表的滚动和焦点
状态变得难以验证。

## 接入

```cmake
add_subdirectory(path/to/paged_list)
target_link_libraries(app PRIVATE paged_list::paged_list)
```

调用方创建行模板并负责把业务缓存投影到行上：

```c
static lv_obj_t *create_file_row(void *ctx, lv_obj_t *parent)
{
    lv_obj_t *row = lv_button_create(parent);
    lv_label_create(row);
    return row;
}

static bool bind_file_row(void *ctx, lv_obj_t *row, uint32_t index,
                          bool selected)
{
    const file_info_t *file = file_cache_at(ctx, index);
    lv_obj_t *label = lv_obj_get_child(row, 0);

    if(file == NULL) {
        lv_label_set_text(label, "Loading...");
        return false;
    }
    lv_label_set_text(label, file->name);
    lv_obj_set_style_bg_opa(row, selected ? LV_OPA_70 : LV_OPA_TRANSP, 0);
    return true;
}

static bool file_item_ready(void *ctx, uint32_t index)
{
    return file_cache_at(ctx, index) != NULL;
}

static void set_file_row_selected(void *ctx, lv_obj_t *row,
                                  uint32_t index, bool selected)
{
    (void)ctx;
    (void)index;
    lv_obj_set_style_bg_opa(row, selected ? LV_OPA_70 : LV_OPA_TRANSP, 0);
}

static void request_files(void *ctx, const paged_list_range_t *range)
{
    file_service_request_range(
        ctx, range->generation, range->first_index, range->last_index);
}
```

```c
const paged_list_config_t config = {
    .item_count = file_service_count(service),
    .row_height = 42,
    .row_gap = 2,
    .visible_rows = 6,
    .prefetch_pages = 1,
    .create_row = create_file_row,
    .bind_row = bind_file_row,
    .set_row_selected = set_file_row_selected,
    .item_ready = file_item_ready,
    .request_range = request_files,
    .on_event = on_file_list_event,
    .user_ctx = service,
};

paged_list_t *files = paged_list_create(content_parent, &config);
```

数据服务收到异步结果后，必须切回 UI 线程，先丢弃旧 generation，再更新自己
的缓存并通知控件。`paged_list_data_changed()` 也会再次校验 generation，作为
接口边界上的防御：

```c
if(reply.generation != paged_list_generation(files)) return;
file_cache_put(service, reply.first, reply.items, reply.count);
(void)paged_list_data_changed(
    files, reply.generation, reply.first, reply.last);
```

当目录、排序或过滤条件发生结构性变化时，服务清空/切换自己的缓存，再调用
`paged_list_reset(files, new_count, wanted_index)`。这会递增 generation；旧请求
结果因 generation 不匹配而被拒绝。加载失败后，调用
`paged_list_retry_visible()` 才会对当前窗口重新发出加载提示，避免焦点抖动或
重复刷新导致的请求风暴。

## 行为与内存

- 页大小固定为 `visible_rows`。`Up/Down` 移动选择，`Left/Right` 翻页，`Enter`
  激活；应用也可直接调用 `paged_list_*` 控制方法映射自己的按键或手势。
- 末页保持相同行内偏移并自动夹紧，默认不循环；只有显式设置
  `wrap_navigation` 才循环。
- 请求范围是当前页加前后 `prefetch_pages` 页。提供 `item_ready` 后，模块只
  请求目标窗口中首个到最后一个未就绪项组成的区间；区间内部可能夹有已缓存
  项，数据服务可自行跳过。`item_ready` 必须是 O(1) 的缓存查询，不能做 I/O。
- 同一未完成范围不会因刷新或同页移动重复请求。部分结果通过
  `paged_list_data_changed()` 回填后，模块会继续请求剩余缺失范围；加载失败
  则由应用决定何时调用 `paged_list_retry_visible()`。
- `data_changed` 只重新绑定与回填范围相交的可见行。同页选择变化优先调用
  `set_row_selected`，仅更新旧、新两行；不提供该回调时才用 `bind_row` 回退。
- RAM 与总文件数无关：模块状态、`visible_rows` 个 slot 指针，以及调用方行模板
  的子树。内部还有 8 项固定事件 FIFO，用于消除回调重入导致的递归和乱序；
  队列容量不足时新的导航会返回 `false`，不会先改状态再丢事件。服务的元数据
  缓存预算由项目自行决定。
- 固定高度行是本模块的前提。可变高度/瀑布流需要另一个布局 module，不能把
  总索引映射规则硬塞进这里。

## 生命周期边界

模块只在 LVGL UI 线程调用。父页面销毁时 root 删除回调会释放列表内部状态；
正常情况下可调用 `paged_list_destroy()`。页面进入后台可保留或销毁该 widget，
由页面的 Presenter/MVU 和 `ui_runtime` Route 策略决定，列表本身不订阅任何服务。

`PAGED_LIST_EVENT_ITEM_ACTIVATED` 只是用户意图。业务层据此执行播放、预览、
删除确认或页面导航；不要在 `bind_row` 中做 I/O 或导航。

`request_range` 和 `on_event` 在列表更新 LVGL 对象树的调用栈内执行。它们可以
更新业务状态，但不能同步销毁列表或它的祖先页面；触发 `ui_runtime` 导航时按既定
规则投递到下一 UI 安全轮次。这样可避免一个事件回调释放仍在执行的列表状态。

`on_event` 中再次调用导航接口是允许的：事件会进入固定 FIFO，当前回调返回后
按产生顺序派发。`create_row`、`bind_row`、`set_row_selected` 和 `item_ready` 是
控件适配回调，不应反向导航、删除控件或执行阻塞操作。

## 输入映射

`config.keymap == NULL` 使用 `Up/Down/Left/Right/Enter`。传入自定义
`paged_list_keymap_t` 可映射设备按键；结构体会在创建时复制，调用方无需长期
持有。传入全零 keymap 可完全关闭内置按键处理，再由应用把旋钮、触摸手势或
组合键转换为 `paged_list_set_selected()`、`paged_list_set_page()` 等动作接口。

## 独立验证

```powershell
cmake -S paged_list -B paged_list/build -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER="D:/lvgl_with_vscode/mingw64/bin/gcc.exe" `
  -DPAGED_LIST_BUILD_TESTS=ON `
  -DPAGED_LIST_LVGL_SOURCE="D:/path/to/lvgl-9.5.0"
cmake --build paged_list/build --parallel
ctest --test-dir paged_list/build --output-on-failure
```

## 可交互示例

构建可交互的 LVGL/SDL 文件列表示例：

```powershell
cmake -S paged_list -B paged_list/demo-build -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER="D:/lvgl_with_vscode/mingw64/bin/gcc.exe" `
  -DPAGED_LIST_BUILD_DEMO=ON `
  -DPAGED_LIST_LVGL_SOURCE="D:/path/to/lvgl-9.5.0" `
  -DPAGED_LIST_SDL2_DIR="D:/xuexi/adr/ui/lv_pc_a/SDL2/lib/cmake/SDL2"
cmake --build paged_list/demo-build --parallel
```

运行 `paged_list/demo-build/paged_list_demo.exe` 会打开窗口。它模拟 1800 条录音
文件、当前页和相邻页的异步元数据加载，以及键盘/鼠标选择与激活；窗口不是
`paged_list_smoke.exe` 那种执行完即退出的自动化测试。
