# Quick Settings

`quick_settings` 是独立的 LVGL 顶层快捷设置浮层，适合挂在 Home、Settings 或任意
页面之上。它提供 Wi-Fi 开关、亮度和音量滑杆、遮罩关闭、上滑关闭以及顶部双指
下滑打开，不包含设备驱动和页面导航。

## 接入

先创建 LVGL target，再加入本目录：

```cmake
add_subdirectory(path/to/lvgl)
add_subdirectory(path/to/quick_settings)
target_link_libraries(app PRIVATE quick_settings::quick_settings)
```

浮层应创建在显示器的顶层，而不是某个页面对象内：

```c
static void on_quick_settings(
    void *ctx, const quick_settings_event_t *event)
{
    switch(event->kind) {
        case QUICK_SETTINGS_EVENT_WIFI_CHANGED:
            device_wifi_set(event->enabled);
            break;
        case QUICK_SETTINGS_EVENT_BRIGHTNESS_CHANGED:
            display_set_brightness(event->value);
            break;
        case QUICK_SETTINGS_EVENT_VOLUME_CHANGED:
            audio_set_volume(event->value);
            break;
        default:
            break;
    }
}

const quick_settings_config_t config = {
    .panel_height = 340,
    .edge_start_height = 56,
    .open_distance = 96,
    .close_distance = 72,
    .animation_duration_ms = 240,
    .on_event = on_quick_settings,
    .user_ctx = app,
};

quick_settings_t *settings =
    quick_settings_create(lv_layer_top(), &config);
```

公共 setter 只同步界面状态，不产生业务回调；用户操作控件时才会上报事件。

## 双指输入

普通 LVGL pointer 输入只包含一个 `point`，不能从它推断双指。触摸驱动或输入适配
层应保留触点 ID，并把每批变化交给模块：

```c
quick_settings_touch_t touches[] = {
    { .id = 0, .x = 180, .y = 24, .pressed = true },
    { .id = 1, .x = 420, .y = 22, .pressed = true },
};

bool claimed = quick_settings_gesture_feed(settings, touches, 2);
```

识别条件是两个触点都从 `edge_start_height` 内开始，两指都以纵向为主向下移动，
平均距离达到 `open_distance`。单指、从页面中部开始或横向双指移动均不会展开。
函数从第二个合格触点按下起返回 `true`，输入适配层应据此屏蔽或取消底层页面的
pointer 流，直到两指全部释放。输入流中断时调用
`quick_settings_gesture_cancel()`。

LVGL 9.3 在 `LV_USE_GESTURE_RECOGNITION=1` 时也提供双指识别。驱动按 LVGL 文档
调用 `lv_indev_gesture_recognizers_update()` 和
`lv_indev_gesture_recognizers_set_data()` 后，可在 `LV_EVENT_GESTURE` 回调中转交：

```c
quick_settings_handle_lvgl_gesture(settings, event);
```

## PC Demo

```powershell
cmake -S quick_settings -B quick_settings/build -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER="D:/lvgl_with_vscode/mingw64/bin/gcc.exe" `
  -DCMAKE_CXX_COMPILER="D:/lvgl_with_vscode/mingw64/bin/g++.exe" `
  -DQUICK_SETTINGS_BUILD_DEMO=ON `
  -DQUICK_SETTINGS_LVGL_SOURCE="D:/lvgl_with_vscode/my_lvgl/lvgl" `
  -DQUICK_SETTINGS_SDL2_DIR="D:/xuexi/adr/ui/lv_pc_a/SDL2/lib/cmake/SDL2"
cmake --build quick_settings/build --parallel
quick_settings/build/quick_settings_demo.exe
```

PC 上可按住 `Ctrl` 从顶部向下拖动，模拟两根同步触点；触摸屏产生的
`SDL_FINGER*` 事件会按真实触点 ID 接入。`Q` 用于直接切换浮层，`Esc` 关闭。

无窗口验证和截图：

```powershell
ctest --test-dir quick_settings/build --output-on-failure
$env:SDL_VIDEODRIVER = "dummy"
quick_settings/build/quick_settings_demo.exe --screenshot `
  quick_settings/build/quick-settings.bmp
```
