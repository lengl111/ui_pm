# Settings Page

`settings_page` 抽取 `ref/unigui` 中设置相关页面的共性布局和操作控件，目标是
迁移到其他 LVGL 项目时只替换数据描述和事件回调，不复制一套页面实现。

## 能力

- 设置总览网格：适合 Wi-Fi、显示、音量、存储、关于等入口；
- 设置列表页：支持分组标题、导航行、操作行、只读值、开关、滑杆和选项下拉；
- 所有行通过稳定的 `row_id` 识别，业务不需要持有内部 LVGL 子对象；
- 统一事件：返回、点击、长按、开关变化、滑杆变化、滑杆释放和选项变化；
- 公共 API 可更新值、开关、选项、显示文本和启用状态；
- `settings_page_set_rows()` 可在页面存活期间原子替换行模型，适合 Wi-Fi 扫描结果刷新；
- Wi-Fi 行支持信号强度、加密、已记住、已连接、连接中和连接失败状态，并可独立切换加载态；
- 行可配置多击阈值，事件携带 `click_count`，适合关于本机中的隐藏入口；
- `SETTINGS_PAGE_STYLE_UNIGUI` 提供与 800x480 unigui 视觉一致的资源皮肤；
- `SETTINGS_PAGE_VARIANT_UNIGUI_BRIGHTNESS`、`_VOLUME`、`_STORAGE`、`_ABOUT`、
  `_ABOUT_DEVICE`、`_MOBILE_NETWORK`、`_WIFI` 和 `_UPGRADE` 提供对应的产品布局；
- 不依赖 `unigui` 图片、设备协议、页面管理器或 LVGL 私有 class；
- 配置和行描述会复制到 Settings Page 对象，字符串、选项数组、字体指针在对象存活期间
  需要保持有效。

库依赖 LVGL 的 `button`、`label`、`switch`、`slider`、`dropdown`、`flex` 和
`grid` 控件。Wi-Fi 加载动画在 `LV_USE_SPINNER` 打开时显示；关闭该控件时其余
Wi-Fi 状态能力仍可用。如果目标项目裁剪了其他控件，需要在 LVGL 配置中打开，或
从行类型中移除对应实现。

## 最小接入

```c
static const settings_page_row_t rows[] = {
    {
        .id = 10,
        .title = "Brightness",
        .subtitle = "Display level",
        .symbol = LV_SYMBOL_IMAGE,
        .kind = SETTINGS_PAGE_ROW_SLIDER,
        .min_value = 0,
        .max_value = 100,
        .step = 5,
        .value = 70,
    },
};

static const settings_page_config_t config = {
    .title = "Settings",
    .layout = SETTINGS_PAGE_LAYOUT_LIST,
    .rows = rows,
    .row_count = 1,
    .show_back = true,
    .on_event = on_settings_event,
    .user_ctx = app,
};

settings_page_t *page = settings_page_create(parent, &config);
```

`parent` 应是应用为页面预留的内容区域。原版运行时的内容区域位于状态栏下方，
尺寸为 `800x420`；状态栏由宿主单独创建，不由本库绘制。`settings_page_demo` 用
同样的两层宿主复现了这个坐标系，因此截图可以直接和原版 800x480 截图比较。

使用 `SETTINGS_PAGE_STYLE_UNIGUI` 时，`assets` 至少应提供背景、标题装饰和返回图标；
亮度和音量变体还需要滑杆旋钮及对应的加减图标。产品标题使用 `title_font`，总览
入口使用 `overview_font`，存储页的大标题使用 `emphasis_font`，其余文本使用
`body_font`。资源和字体指针只需在页面存活期间有效。

Wi-Fi 变体将第一个 `SETTINGS_PAGE_ROW_SWITCH` 作为无线开关，其余行按 `section`
分组；未设置 `section` 的第一行作为当前网络，后续行作为网络列表。每个网络行仍
通过普通的 `ROW_NAVIGATION`/`ROW_ACTION` 事件交给业务处理，短按和长按分别映射到
`SETTINGS_PAGE_EVENT_ROW_ACTIVATED` 与 `SETTINGS_PAGE_EVENT_ROW_LONG_PRESSED`。
`section_subtitle` 可为分组提供第二行说明文本。

可以直接在行描述中提供 Wi-Fi 状态，也可以在运行时更新：

```c
static const settings_page_wifi_state_t state = {
    .encrypted = true,
    .remembered = false,
    .signal_strength = -68,
    .status = SETTINGS_PAGE_WIFI_STATUS_CONNECTING,
};

settings_page_set_wifi_state(page, wifi_row_id, &state);
settings_page_set_wifi_loading(page, true);
```

`wifi_connected_src`、`wifi_encrypted_src` 和三个 `wifi_signal_*_src` 资源用于
替换原版 Wi-Fi 图标。未提供信号强度时，兼容旧行描述中的 `image_src`。

存储变体把第 0 行的 `value` 作为 `0..100` 的占用百分比，因此可以直接调用
`settings_page_set_value()` 更新进度条；第 0 行的 `value_text` 和第 1 行的
`value_text` 分别显示可用空间和总容量。

业务回调根据 `event->kind` 和 `event->row_id` 执行导航或设备操作。库只负责控件
状态和事件，不替业务决定 Wi-Fi 连接、亮度写入或恢复出厂流程。

默认点击阈值为一次。将 `activation_clicks` 设为大于 1 可延迟激活，
`activation_window_ms` 默认为 2000；达到阈值后事件中的 `click_count` 才会变为
对应次数。页面会在可用的控件和行上注册 `LV_EVENT_KEY`，因此宿主可以把 `Esc`
交给页面统一产生返回事件。宿主仍负责维护页面栈；Demo 展示了总览 -> 关于 -> 关于
本机/系统升级的返回路径。

`settings_page_set_value()`、`settings_page_set_checked()`、
`settings_page_set_choice()`、`settings_page_set_value_text()`、
`settings_page_set_enabled()`、`settings_page_set_rows()`、
`settings_page_set_wifi_state()` 和 `settings_page_set_wifi_loading()` 只更新控件和
内部状态，不主动触发业务回调；用户实际操作控件时才会发送对应事件。

## 构建

作为已有 LVGL target 的子目录：

```cmake
add_subdirectory(path/to/lvgl)
add_subdirectory(path/to/settings_page)
target_link_libraries(app PRIVATE settings_page::settings_page)
```

本目录的 PC Demo 使用当前仓库的 LVGL 9 和 SDL2：

```powershell
cmake -S settings_page -B settings_page/build -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER="D:/lvgl_with_vscode/mingw64/bin/gcc.exe" `
  -DCMAKE_CXX_COMPILER="D:/lvgl_with_vscode/mingw64/bin/g++.exe" `
  -DSETTINGS_PAGE_BUILD_DEMO=ON `
  -DSETTINGS_PAGE_LVGL_SOURCE="D:/lvgl_with_vscode/my_lvgl/lvgl" `
  -DSETTINGS_PAGE_SDL2_DIR="D:/xuexi/adr/ui/lv_pc_a/SDL2/lib/cmake/SDL2"
cmake --build settings_page/build --parallel
settings_page/build/settings_page_demo.exe
```

无窗口验证：

```powershell
ctest --test-dir settings_page/build --output-on-failure
```

`settings_page_demo --smoke` 会验证总览进入子页、返回、开关、滑杆、公共状态
更新和真实 SDL 鼠标事件；Demo 中的设备数据和操作反馈均为模拟实现。

生成与原版同尺寸的页面截图：

```powershell
$env:SDL_VIDEODRIVER = "dummy"
settings_page/build/settings_page_demo.exe --screenshots settings_page/build/screenshots
```
