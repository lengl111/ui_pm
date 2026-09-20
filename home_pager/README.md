# Home Pager

`home_pager` 提炼自 `ref/unigui/src/lv_home_page.c` 的 Home 布局行为，只负责
横向多页选择，不负责状态栏、导航、业务状态或页面生命周期。

当前实现保留了原页面的关键体验：

- 横向拖动并按页首吸附，一次手势最多切换一页；
- 首尾页循环连接，从第一页继续右滑会回到最后一页，反向同理；
- 慢速拖动超过相邻页间距的 20% 才切页，快速 flick 可用短行程切页；
- 2 至 8 页，每页 1 至 4 个入口；
- 4 项页使用两个大入口和两个小入口，3 项页使用一个大入口和两个小入口；
- 滑动过程中分页指示器按实际滚动进度连续伸缩和淡入淡出；
- 默认按需创建页内控件，也可关闭懒加载；
- 点击入口只上报 `item_id`，调用方决定是否导航；
- 图片、标题字体和状态栏高度由调用方提供。

PC Demo 的回调额外提供了一个模拟导航：点击入口会打开对应的模拟功能页，点击
左上角 `Home` 返回。这个模拟页只属于 Demo，不会改变 `home_pager` 的业务边界。

## 分页行为

- 页面使用 LVGL 原生 `SNAPPABLE`、`SCROLL_ONE` 和
  `LV_SCROLL_SNAP_START`，同时保留横向滚动惯性。
- 循环分页通过首尾各一个物理副本实现。用户看到的逻辑页码仍然是
  `0` 到 `page_count - 1`，跨过边界后会无动画校正到真实页，不会把副本暴露给
  `PAGE_CHANGED` 或 `home_pager_get_page()`。
- 松手目标以手势开始页为基准计算，不以松手瞬间的最近页为基准，避免慢拖时
  因越过中点而意外翻页。
- 分页位置来自布局后的真实坐标，不假设 `页码 * 屏幕宽度`；容器尺寸变化后会
  重新计算并吸附到当前页。动画中发生尺寸变化时，最终目标也会按新尺寸校正。
- 指示器使用固定宽度轨道和绝对定位。当前页与下一页的 pill 宽度、透明度随
  滚动连续插值，不会因 flex 重排导致轨道左右抖动。
- `HOME_PAGER_EVENT_PAGE_CHANGED` 只在滚动稳定且页码实际变化后发送；
  `home_pager_get_page()` 在动画期间仍返回上一个稳定页。
- 懒加载模式首次只创建初始页；手势开始时预建相邻页，跨页程序动画会预建
  路径上的页面，避免滑动途中出现空页。

## 接入

先创建 LVGL target，再加入本目录：

```cmake
add_subdirectory(path/to/lvgl)
add_subdirectory(path/to/home_pager)
target_link_libraries(app PRIVATE home_pager::home_pager)
```

最小数据配置：

```c
static const home_pager_item_t first_page_items[] = {
    {
        .id = 1,
        .title = "Camera",
        .symbol = LV_SYMBOL_IMAGE,
        .accent = HOME_PAGER_ACCENT_YELLOW,
    },
    {
        .id = 2,
        .title = "Settings",
        .symbol = LV_SYMBOL_SETTINGS,
        .accent = HOME_PAGER_ACCENT_BLUE,
    },
};

static const home_pager_page_t pages[] = {
    { .items = first_page_items, .item_count = 2 },
};

home_pager_config_t config = {
    .pages = pages,
    .page_count = 1,
    .lazy_load = true,
    .top_inset = 60,
    .on_event = on_home_event,
};

home_pager_t *pager = home_pager_create(parent, &config);
```

配置中的页和条目结构会被复制；字符串、图片源和字体指针在 Pager 存活期间
必须保持有效。`image_src` 可传入任意 LVGL 支持的图片源。若图片本身已经包含
标题，可把条目的 `title` 留空。

`home_pager_destroy()` 会删除整个内容子树。若父对象先被删除，Pager 也会自动
释放，此后调用方保存的 `home_pager_t *` 不再有效。

## PC 演示

演示使用仓库现有 LVGL 9、SDL2 和 unigui 裁剪字体：

```powershell
cmake -S home_pager -B home_pager/build -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER="D:/lvgl_with_vscode/mingw64/bin/gcc.exe" `
  -DCMAKE_CXX_COMPILER="D:/lvgl_with_vscode/mingw64/bin/g++.exe" `
  -DHOME_PAGER_BUILD_DEMO=ON `
  -DHOME_PAGER_LVGL_SOURCE="D:/lvgl_with_vscode/my_lvgl/lvgl" `
  -DHOME_PAGER_SDL2_DIR="D:/xuexi/adr/ui/lv_pc_a/SDL2/lib/cmake/SDL2"
cmake --build home_pager/build --parallel
home_pager/build/home_pager_demo.exe
```

无窗口验证和四页截图：

```powershell
ctest --test-dir home_pager/build --output-on-failure
New-Item -ItemType Directory home_pager/build/screenshots -Force | Out-Null
$env:SDL_VIDEODRIVER='dummy'
home_pager/build/home_pager_demo.exe --screenshots home_pager/build/screenshots
```
