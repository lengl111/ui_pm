# Photo Gallery

`photo_gallery` 提炼了 `ref/unigui` 相册中的通用 UI 能力：大数据量图片的
缩略图网格、固定缓存复用、范围预取、按页滚动吸附、选择状态和单图左右浏览。
模块只依赖 LVGL，不依赖 `unigui`、文件系统、图片解码器或页面管理器。

## 提炼范围

来自 `ref/unigui/src/lv_albums_page.c` 的可复用部分：

- 3 列缩略图网格和可调布局参数；
- 固定数量 cache slot，滚动时复用控件，不按全量照片创建对象；
- 可见范围外的预取窗口；
- 数据未到达时通过 `request_range` 请求索引区间；
- 滚动结束按页吸附，一次手势最多跨一页；
- 点击回调与多选状态分离。

来自 `ref/unigui/src/lv_picture_page.c` 和 `lv_control.c` 的可复用部分：

- 独立查看器覆盖层；
- 点击缩略图进入指定索引；
- 左右滑动切换相邻图片；
- 到达第一张/最后一张时上报边界事件；
- 图片源、标题和业务行为通过回调注入。

没有复制 `albums_model` 的链表、UDS 消息、`page_manager`、硬编码存储路径或同步磁盘读取。真实图片状态由应用/媒体服务拥有。

## 最小接入

已有 LVGL target 时：

```cmake
add_subdirectory(path/to/photo_gallery)
target_link_libraries(app PRIVATE photo_gallery::photo_gallery)
```

静态小列表可以直接提供 `items`：

```c
static const photo_gallery_item_t photos[] = {
    { .id = 101, .thumbnail_src = &thumb_0,
      .image_src = &image_0, .title = "Photo 1" },
    { .id = 102, .thumbnail_src = &thumb_1,
      .image_src = &image_1, .title = "Photo 2" },
};

const photo_gallery_config_t config = {
    .items = photos,
    .item_count = 2,
    .columns = 3,
    .rows_per_page = 2,
    .cache_size = 12,
    .prefetch_items = 3,
    .snap_to_page = true,
    .title = "Photos",
    .on_event = on_gallery_event,
    .user_ctx = app,
};

photo_gallery_t *gallery =
    photo_gallery_create(lv_screen_active(), &config);
```

大相册应使用 `item_provider`，不要把所有图片数据复制进 UI：

```c
static bool provide_item(
    void *ctx, uint32_t index, photo_gallery_item_t *item)
{
    media_item_t *media = media_get_item(ctx, index);
    if (media == NULL) return false; /* 仍在加载 */
    item->id = media->id;
    item->thumbnail_src = media->thumbnail_src;
    item->image_src = media->image_src;
    item->title = media->name;
    return true;
}

static void request_range(void *ctx, uint32_t from, uint32_t to)
{
    media_request_metadata(ctx, from, to);
}
```

当媒体服务把数据放入自己的缓存后，在 UI 线程调用：

```c
photo_gallery_data_changed(gallery, from, to);
```

如果总数变化，调用 `photo_gallery_set_item_count()`。缩减或扩展数量时会保留仍存在的索引选择状态；如果新选择缓冲分配失败，旧数量和状态保持不变。索引区间 `from/to` 均为包含端点。

## 交互接口

- `photo_gallery_set_page()`：程序切换到指定逻辑页。
- `photo_gallery_set_selection_mode()`：切换点击行为为多选或打开查看器事件。
- `photo_gallery_set_item_selected()`、`photo_gallery_clear_selection()`：同步选择状态。
- `photo_gallery_show_viewer()`：按索引打开查看器。
- `photo_gallery_viewer_set_index()`：程序切换查看器索引。
- `photo_gallery_close_viewer()`：关闭查看器。

事件包括：

- `PHOTO_GALLERY_EVENT_ITEM_CLICKED`：普通模式点击缩略图，业务通常在这里打开查看器或进入其他页面；
- `PHOTO_GALLERY_EVENT_SELECTION_CHANGED`：多选变化；
- `PHOTO_GALLERY_EVENT_PAGE_CHANGED`：列表稳定吸附到新页后触发；
- `PHOTO_GALLERY_EVENT_VIEWER_OPENED/CLOSED`：查看器生命周期；
- `PHOTO_GALLERY_EVENT_VIEWER_INDEX_CHANGED`：查看器切换图片；
- `PHOTO_GALLERY_EVENT_VIEWER_EDGE_REACHED`：已经到达第一张或最后一张。

公共 setter 只同步 UI 状态，不替业务执行删除、导航、文件读取或硬件操作。

## 分页和缓存行为

- `columns * rows_per_page` 是一页的逻辑项目数；默认 `3 * 2`。
- `cache_size` 是实际 LVGL 缩略图控件数，至少覆盖一页，最大为 `PHOTO_GALLERY_MAX_CACHE_ITEMS`。
- 缓存只保留当前可见范围及邻近 `prefetch_items`，旧 slot 用最近最少使用策略复用。
- 滚动列表可启用 `snap_to_page`；释放时以手势开始页为基准，慢拖超过 20% 或 flick 才切换一页。
- `PAGE_CHANGED` 只在稳定页改变后触发，不能把滚动过程当作业务页码变化。
- 模块本身不做首尾循环。相册查看器到边界上报事件，由产品决定是否循环、提示或退出。

注意：`photo_gallery_item_t` 中的 `thumbnail_src`、`image_src`、`title` 指针必须在 gallery 存活期间有效；异步 provider 可返回 `false`，数据就绪后通过 `photo_gallery_data_changed()` 刷新。

## PC Demo

```powershell
cmake -S photo_gallery -B photo_gallery/build -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER="D:/lvgl_with_vscode/mingw64/bin/gcc.exe" `
  -DCMAKE_CXX_COMPILER="D:/lvgl_with_vscode/mingw64/bin/g++.exe" `
  -DPHOTO_GALLERY_BUILD_DEMO=ON `
  -DPHOTO_GALLERY_LVGL_SOURCE="D:/lvgl_with_vscode/my_lvgl/lvgl" `
  -DPHOTO_GALLERY_SDL2_DIR="D:/xuexi/adr/ui/lv_pc_a/SDL2/lib/cmake/SDL2"
cmake --build photo_gallery/build --parallel
photo_gallery/build/photo_gallery_demo.exe
```

Demo 行为：

- 鼠标上下拖动列表，释放后按页吸附；
- 点击缩略图打开单图查看器；
- 查看器中点击左右按钮或横向拖动切换图片；
- `Esc` 可关闭查看器；
- `--smoke` 验证选择、分页数量、查看器打开和索引切换；
- `--screenshot FILE` 在 dummy SDL 下输出初始列表截图。

验证命令：

```powershell
$env:SDL_VIDEODRIVER = "dummy"
ctest --test-dir photo_gallery/build --output-on-failure
photo_gallery/build/photo_gallery_demo.exe --smoke
photo_gallery/build/photo_gallery_demo.exe --screenshot photo_gallery/build/photo-gallery.bmp
```

## 目标板移植注意事项

1. 触摸、媒体服务回调和所有 LVGL API 必须在 UI 线程执行；跨线程只投递索引范围和状态消息。
2. `item_provider` 不应在 LVGL 线程同步读取大文件。解码、缩放和 IO 放在媒体任务，UI 只接收已准备好的图片源。
3. 缩略图和原图使用不同资源预算；缩略图缓存命中不代表原图已经可用。
4. 若图片源是动态内存，服务必须保证其生命周期覆盖 `lv_image_set_src` 使用期，并在替换/回收前确保 UI 不再引用。
5. 与统一页面管理器集成时，查看器最好作为宿主 Overlay 或页面内部子树，由宿主负责返回键、Modal 和输入优先级；不要让模块创建独立 LVGL screen。
6. 在目标分辨率、旋转方向和真实字体下重新测量 thumbnail 尺寸、页高、触摸阈值、帧率和 LVGL heap 峰值。

## 已知边界

- 当前查看器是单图覆盖层，不包含缩放、双指捏合、旋转、删除确认或高清原图切换。
- 当前模块不提供图片解码和资源缓存；它只管理 LVGL 对象与交互状态。
- `snap_to_page` 是垂直分页网格；需要连续瀑布流时应另建布局策略，不要硬改页吸附阈值。
- 查看器默认不循环，边界由事件交给业务决定。
