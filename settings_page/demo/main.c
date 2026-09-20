#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <lvgl.h>

#include "settings_page/settings_page.h"

LV_FONT_DECLARE(settings_demo_cn_20)
LV_FONT_DECLARE(settings_demo_cn_28)
LV_FONT_DECLARE(SourceHanSansCN_Regular_24px_4bpp)
LV_FONT_DECLARE(SourceHanSansCN_Medium_24px_4bpp)
LV_FONT_DECLARE(SourceHanSansCN_Regular_32px_4bpp)
LV_FONT_DECLARE(SourceHanSansCN_Medium_32px_4bpp)
LV_FONT_DECLARE(SourceHanSansCN_Medium_28px_4bpp)
LV_FONT_DECLARE(SourceHanSansCN_Regular_16px_4bpp)
LV_FONT_DECLARE(ui_font_MED36)

static lv_font_t settings_demo_product_font;
static lv_font_t settings_demo_body_font;

#define SETTINGS_DEMO_WIDTH 800
#define SETTINGS_DEMO_HEIGHT 480

#define SETTINGS_ASSET(name) "A:/usr/gui/image/icon/" name ".png"

static const settings_page_assets_t unigui_assets = {
    .background_src = SETTINGS_ASSET("005"),
    .title_background_src = SETTINGS_ASSET("006"),
    .back_src = SETTINGS_ASSET("back"),
    .arrow_src = SETTINGS_ASSET("arrow"),
    .slider_knob_src = SETTINGS_ASSET("sliderHead"),
    .decrement_src = SETTINGS_ASSET("volumeDown"),
    .increment_src = SETTINGS_ASSET("volumeUp"),
    .wifi_connected_src = SETTINGS_ASSET("wifi1"),
    .wifi_encrypted_src = SETTINGS_ASSET("wifi2"),
    .wifi_signal_weak_src = SETTINGS_ASSET("wifi3"),
    .wifi_signal_medium_src = SETTINGS_ASSET("wifi4"),
    .wifi_signal_strong_src = SETTINGS_ASSET("wifi5"),
    .overview_item_height = 160,
    .overview_icon_width = 102,
    .overview_icon_height = 100,
    .overview_label_gap = 20,
    .decrement_step = 10,
    .increment_step = 10,
};

enum {
    SETTINGS_ROW_WIFI = 1,
    SETTINGS_ROW_MOBILE,
    SETTINGS_ROW_DISPLAY,
    SETTINGS_ROW_VOLUME,
    SETTINGS_ROW_STORAGE,
    SETTINGS_ROW_ABOUT,
};

enum {
    WIFI_ROW_NETWORK = 101,
    WIFI_ROW_ENABLED,
    WIFI_ROW_ADD,
    DISPLAY_ROW_BRIGHTNESS = 201,
    DISPLAY_ROW_SCREEN_OFF,
    DISPLAY_ROW_SHUTDOWN,
    VOLUME_ROW_LEVEL = 301,
    ABOUT_ROW_UPDATE = 401,
    ABOUT_ROW_DEVICE,
    STORAGE_ROW_USAGE = 501,
    STORAGE_ROW_TOTAL,
    DEVICE_ROW_MODEL = 601,
    DEVICE_ROW_MAC,
    DEVICE_ROW_SN,
    DEVICE_ROW_VERSION,
    DEVICE_ROW_IMEI,
    DEVICE_ROW_CMEI,
    DEVICE_ROW_FACTORY_RESET,
    DEVICE_ROW_MOBILE_QRCODE,
    DEVICE_ROW_SERVICE,
    DEVICE_ROW_PRIVACY,
    DEVICE_ROW_USER_RULE,
    DEVICE_ROW_AIGC,
    UPGRADE_ROW_START = 701,
};

static const settings_page_assets_t brightness_assets = {
    .background_src = SETTINGS_ASSET("005"),
    .title_background_src = SETTINGS_ASSET("006"),
    .back_src = SETTINGS_ASSET("back"),
    .arrow_src = SETTINGS_ASSET("arrow"),
    .slider_knob_src = SETTINGS_ASSET("sliderHead"),
    .decrement_src = SETTINGS_ASSET("lightDown"),
    .increment_src = SETTINGS_ASSET("lightUp"),
    .wifi_connected_src = SETTINGS_ASSET("wifi1"),
    .wifi_encrypted_src = SETTINGS_ASSET("wifi2"),
    .wifi_signal_weak_src = SETTINGS_ASSET("wifi3"),
    .wifi_signal_medium_src = SETTINGS_ASSET("wifi4"),
    .wifi_signal_strong_src = SETTINGS_ASSET("wifi5"),
    .overview_item_height = 160,
    .overview_icon_width = 102,
    .overview_icon_height = 100,
    .overview_label_gap = 20,
    .decrement_step = 20,
    .increment_step = 20,
};

static const char *const sleep_options[] = {
    "5分钟",
    "10分钟",
    "15分钟",
    "30分钟",
};

static const char *const shutdown_options[] = {
    "5分钟",
    "15分钟",
    "30分钟",
    "永不",
};

typedef struct {
    lv_display_t *display;
    lv_obj_t *content_host;
    lv_obj_t *status_bar;
    settings_page_t *page;
    lv_obj_t *feedback;
    uint32_t window_id;
    uint32_t event_count;
    uint16_t last_row_id;
    int32_t last_value;
    settings_page_event_kind_t last_event_kind;
    uint8_t last_click_count;
    uint8_t pending_view;
    bool showing_overview;
    bool navigation_pending;
    uint8_t current_view;
    uint8_t view_stack[8];
    uint8_t view_depth;
} settings_demo_t;

typedef enum {
    SETTINGS_DEMO_OVERVIEW = 0,
    SETTINGS_DEMO_WIFI,
    SETTINGS_DEMO_MOBILE,
    SETTINGS_DEMO_DISPLAY,
    SETTINGS_DEMO_VOLUME,
    SETTINGS_DEMO_STORAGE,
    SETTINGS_DEMO_ABOUT,
    SETTINGS_DEMO_ABOUT_DEVICE,
    SETTINGS_DEMO_UPGRADE,
} settings_demo_view_t;


static void settings_demo_pump(settings_demo_t *demo, uint32_t duration_ms)
{
    uint32_t start = lv_tick_get();

    do {
        (void)lv_timer_handler();
        lv_refr_now(demo->display);
        SDL_Delay(2u);
    } while(lv_tick_elaps(start) < duration_ms);
}

static bool settings_demo_push_mouse_button(
    settings_demo_t *demo, uint32_t type, int32_t x, int32_t y)
{
    SDL_Event event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.button.windowID = demo->window_id;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.state =
        type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    event.button.clicks = 1u;
    event.button.x = x;
    event.button.y = y;
    return SDL_PushEvent(&event) == 1;
}

static bool settings_demo_write_snapshot(
    settings_demo_t *demo, const char *path)
{
    lv_draw_buf_t *snapshot;
    FILE *file;
    uint32_t row_size;
    uint32_t image_size;

    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(demo->display);
    snapshot = lv_snapshot_take(
        lv_screen_active(), LV_COLOR_FORMAT_RGB888);
    if(snapshot == NULL) return false;
    file = fopen(path, "wb");
    if(file == NULL) {
        lv_draw_buf_destroy(snapshot);
        return false;
    }
    row_size = (snapshot->header.w * 3u + 3u) & ~UINT32_C(3);
    image_size = row_size * snapshot->header.h;
    (void)fwrite("BM", 2u, 1u, file);
    {
        uint8_t header[52] = {0};
        uint32_t file_size = 54u + image_size;
        uint32_t dib_size = 40u;
        uint32_t width = snapshot->header.w;
        uint32_t height = snapshot->header.h;
        uint16_t planes = 1u;
        uint16_t bits = 24u;
        uint32_t offset = 54u;

        memcpy(header, &file_size, sizeof(file_size));
        memcpy(header + 8, &offset, sizeof(offset));
        memcpy(header + 12, &dib_size, sizeof(dib_size));
        memcpy(header + 16, &width, sizeof(width));
        memcpy(header + 20, &height, sizeof(height));
        memcpy(header + 24, &planes, sizeof(planes));
        memcpy(header + 26, &bits, sizeof(bits));
        memcpy(header + 32, &image_size, sizeof(image_size));
        (void)fwrite(header, sizeof(header), 1u, file);
    }
    for(int32_t y = (int32_t)snapshot->header.h - 1; y >= 0; --y) {
        const uint8_t *row =
            snapshot->data + (uint32_t)y * snapshot->header.stride;
        uint32_t x;

        for(x = 0u; x < snapshot->header.w; ++x) {
            uint8_t bgr[3] = {
                row[x * 3u + 2u],
                row[x * 3u + 1u],
                row[x * 3u],
            };
            (void)fwrite(bgr, sizeof(bgr), 1u, file);
        }
        for(x = snapshot->header.w * 3u; x < row_size; ++x) {
            (void)fputc(0, file);
        }
    }
    (void)fclose(file);
    lv_draw_buf_destroy(snapshot);
    return true;
}

static bool settings_demo_click(
    settings_demo_t *demo, int32_t x, int32_t y)
{
    if(!settings_demo_push_mouse_button(
           demo, SDL_MOUSEBUTTONDOWN, x, y)) {
        return false;
    }
    settings_demo_pump(demo, 20u);
    if(!settings_demo_push_mouse_button(
           demo, SDL_MOUSEBUTTONUP, x, y)) {
        return false;
    }
    settings_demo_pump(demo, 40u);
    return true;
}

static bool settings_demo_long_press(
    settings_demo_t *demo, int32_t x, int32_t y)
{
    if(!settings_demo_push_mouse_button(
           demo, SDL_MOUSEBUTTONDOWN, x, y)) {
        return false;
    }
    settings_demo_pump(demo, 700u);
    if(!settings_demo_push_mouse_button(
           demo, SDL_MOUSEBUTTONUP, x, y)) {
        return false;
    }
    settings_demo_pump(demo, 50u);
    return true;
}

static void settings_demo_show_status(settings_demo_t *demo, const char *text)
{
    if(demo->feedback == NULL) return;
    lv_label_set_text(demo->feedback, text != NULL ? text : "");
    lv_obj_remove_flag(demo->feedback, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(demo->feedback);
}

static lv_obj_t *settings_demo_create_status_bar(settings_demo_t *demo)
{
    lv_obj_t *bar;
    lv_obj_t *label;
    lv_obj_t *image;

    bar = lv_obj_create(lv_screen_active());
    if(bar == NULL) return NULL;
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, SETTINGS_DEMO_WIDTH, 60);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x242424), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    label = lv_label_create(bar);
    if(label == NULL) return NULL;
    lv_label_set_text(label, "9月4日 12:00");
    lv_obj_set_size(label, 260, 40);
    lv_obj_set_pos(label, 20, 15);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &SourceHanSansCN_Regular_24px_4bpp, 0);

    label = lv_label_create(bar);
    if(label == NULL) return NULL;
    lv_label_set_text(label, "x");
    lv_obj_set_size(label, 16, 24);
    lv_obj_set_pos(label, 632, 18);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &settings_demo_cn_20, 0);

    image = lv_image_create(bar);
    if(image == NULL) return NULL;
    lv_image_set_src(image, SETTINGS_ASSET("status_signal"));
    lv_obj_set_size(image, 28, 18);
    lv_obj_set_pos(image, 634, 21);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);

    image = lv_image_create(bar);
    if(image == NULL) return NULL;
    lv_image_set_src(image, SETTINGS_ASSET("status_wifi"));
    lv_obj_set_size(image, 28, 20);
    lv_obj_set_pos(image, 681, 20);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);

    image = lv_image_create(bar);
    if(image == NULL) return NULL;
    lv_image_set_src(image, SETTINGS_ASSET("status_battery"));
    lv_obj_set_size(image, 51, 24);
    lv_obj_set_pos(image, 729, 18);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);

    demo->status_bar = bar;
    return bar;
}

static const settings_page_row_t overview_rows[] = {
    {
        .id = SETTINGS_ROW_WIFI,
        .title = "Wi-Fi",
        .kind = SETTINGS_PAGE_ROW_NAVIGATION,
        .accent = SETTINGS_PAGE_ACCENT_BLUE,
        .image_src = SETTINGS_ASSET("app1"),
    },
    {
        .id = SETTINGS_ROW_MOBILE,
        .title = "移动网络",
        .kind = SETTINGS_PAGE_ROW_NAVIGATION,
        .accent = SETTINGS_PAGE_ACCENT_BLUE,
        .image_src = SETTINGS_ASSET("app2"),
    },
    {
        .id = SETTINGS_ROW_DISPLAY,
        .title = "显示",
        .kind = SETTINGS_PAGE_ROW_NAVIGATION,
        .accent = SETTINGS_PAGE_ACCENT_BLUE,
        .image_src = SETTINGS_ASSET("app4"),
    },
    {
        .id = SETTINGS_ROW_VOLUME,
        .title = "音量",
        .kind = SETTINGS_PAGE_ROW_NAVIGATION,
        .accent = SETTINGS_PAGE_ACCENT_BLUE,
        .image_src = SETTINGS_ASSET("app3"),
    },
    {
        .id = SETTINGS_ROW_STORAGE,
        .title = "存储",
        .kind = SETTINGS_PAGE_ROW_NAVIGATION,
        .accent = SETTINGS_PAGE_ACCENT_BLUE,
        .image_src = SETTINGS_ASSET("app5"),
    },
    {
        .id = SETTINGS_ROW_ABOUT,
        .title = "关于",
        .kind = SETTINGS_PAGE_ROW_NAVIGATION,
        .accent = SETTINGS_PAGE_ACCENT_BLUE,
        .image_src = SETTINGS_ASSET("app6"),
    },
};

static const settings_page_row_t wifi_rows[] = {
    {
        .id = WIFI_ROW_ENABLED,
        .title = "无线局域网",
        .kind = SETTINGS_PAGE_ROW_SWITCH,
        .accent = SETTINGS_PAGE_ACCENT_BLUE,
        .checked = true,
    },
    {
        .id = WIFI_ROW_NETWORK,
        .title = "UNI-Lab",
        .kind = SETTINGS_PAGE_ROW_NAVIGATION,
        .accent = SETTINGS_PAGE_ACCENT_BLUE,
        .image_src = SETTINGS_ASSET("wifi5"),
        .wifi_remembered = true,
        .wifi_signal_strength = -45,
        .wifi_status = SETTINGS_PAGE_WIFI_STATUS_CONNECTED,
    },
    {
        .id = WIFI_ROW_ADD,
        .section = "我的网络",
        .title = "Home-2.4G",
        .kind = SETTINGS_PAGE_ROW_NAVIGATION,
        .accent = SETTINGS_PAGE_ACCENT_BLUE,
        .image_src = SETTINGS_ASSET("wifi5"),
        .wifi_encrypted = true,
        .wifi_remembered = true,
        .wifi_signal_strength = -55,
    },
    {
        .id = 104,
        .section = "其他网络",
        .section_subtitle =
            "如需使用手机热点,请使用2.4GHz频段或打开“最大兼容性”开关",
        .title = "Guest",
        .kind = SETTINGS_PAGE_ROW_NAVIGATION,
        .accent = SETTINGS_PAGE_ACCENT_BLUE,
        .image_src = SETTINGS_ASSET("wifi3"),
        .wifi_encrypted = true,
        .wifi_signal_strength = -88,
    },
    {
        .id = 105,
        .section = "其他网络",
        .title = "Office-5G",
        .kind = SETTINGS_PAGE_ROW_NAVIGATION,
        .accent = SETTINGS_PAGE_ACCENT_BLUE,
        .image_src = SETTINGS_ASSET("wifi4"),
        .wifi_encrypted = true,
        .wifi_signal_strength = -65,
    },
};

static const settings_page_row_t display_rows[] = {
    {
        .id = DISPLAY_ROW_BRIGHTNESS,
        .title = "亮度调节",
        .kind = SETTINGS_PAGE_ROW_SLIDER,
        .accent = SETTINGS_PAGE_ACCENT_YELLOW,
        .value = 60,
        .min_value = 0,
        .max_value = 100,
        .step = 1,
    },
    {
        .id = DISPLAY_ROW_SCREEN_OFF,
        .title = "熄屏时间",
        .kind = SETTINGS_PAGE_ROW_CHOICE,
        .accent = SETTINGS_PAGE_ACCENT_YELLOW,
        .selected_option = 1,
        .options = sleep_options,
        .option_count = 4,
    },
    {
        .id = DISPLAY_ROW_SHUTDOWN,
        .title = "关机时间",
        .kind = SETTINGS_PAGE_ROW_CHOICE,
        .accent = SETTINGS_PAGE_ACCENT_YELLOW,
        .selected_option = 3,
        .options = shutdown_options,
        .option_count = 4,
    },
};

static const settings_page_row_t volume_rows[] = {
    {
        .id = VOLUME_ROW_LEVEL,
        .title = "音量",
        .kind = SETTINGS_PAGE_ROW_SLIDER,
        .accent = SETTINGS_PAGE_ACCENT_ORANGE,
        .value = 60,
        .min_value = 0,
        .max_value = 100,
        .step = 1,
    },
};

static const settings_page_row_t mobile_rows[] = {
    {
        .id = 502,
        .title = "移动网络",
        .kind = SETTINGS_PAGE_ROW_SWITCH,
        .accent = SETTINGS_PAGE_ACCENT_BLUE,
        .checked = true,
    },
};

static const settings_page_row_t mobile_rows_refreshed[] = {
    {
        .id = 502,
        .title = "移动网络",
        .kind = SETTINGS_PAGE_ROW_SWITCH,
        .accent = SETTINGS_PAGE_ACCENT_BLUE,
        .checked = false,
    },
};

static const settings_page_row_t storage_rows[] = {
    {
        .id = STORAGE_ROW_USAGE,
        .title = "外部存储",
        .kind = SETTINGS_PAGE_ROW_VALUE,
        .accent = SETTINGS_PAGE_ACCENT_PURPLE,
        .value = 48,
        .value_text = "可用 4.2 GB",
    },
    {
        .id = STORAGE_ROW_TOTAL,
        .title = "总容量",
        .kind = SETTINGS_PAGE_ROW_VALUE,
        .accent = SETTINGS_PAGE_ACCENT_PURPLE,
        .value_text = "总共 8.0 GB",
    },
};

static const settings_page_row_t about_rows[] = {
    {
        .id = ABOUT_ROW_UPDATE,
        .title = "系统升级",
        .kind = SETTINGS_PAGE_ROW_NAVIGATION,
        .accent = SETTINGS_PAGE_ACCENT_BLUE,
        .value_text = "New",
        .image_src = SETTINGS_ASSET("icon5"),
    },
    {
        .id = ABOUT_ROW_DEVICE,
        .title = "关于本机",
        .kind = SETTINGS_PAGE_ROW_NAVIGATION,
        .accent = SETTINGS_PAGE_ACCENT_BLUE,
        .image_src = SETTINGS_ASSET("icon6"),
    },
};

static const settings_page_row_t about_device_rows[] = {
    {
        .id = DEVICE_ROW_MODEL,
        .title = "型号",
        .kind = SETTINGS_PAGE_ROW_VALUE,
        .value_text = "UNI-PC-SIM",
        .activation_clicks = 5,
    },
    {
        .id = DEVICE_ROW_MAC,
        .title = "MAC",
        .kind = SETTINGS_PAGE_ROW_VALUE,
        .value_text = "02:00:00:00:00:01",
    },
    {
        .id = DEVICE_ROW_SN,
        .title = "SN",
        .kind = SETTINGS_PAGE_ROW_VALUE,
        .value_text = "SIM-20260904",
        .activation_clicks = 5,
    },
    {
        .id = DEVICE_ROW_VERSION,
        .title = "版本",
        .kind = SETTINGS_PAGE_ROW_VALUE,
        .value_text = "1.0.0-pc",
    },
    {
        .id = DEVICE_ROW_IMEI,
        .title = "IMEI",
        .kind = SETTINGS_PAGE_ROW_VALUE,
        .value_text = "860000000000001",
        .activation_clicks = 5,
    },
    {
        .id = DEVICE_ROW_CMEI,
        .title = "CMEI",
        .kind = SETTINGS_PAGE_ROW_VALUE,
        .value_text = "860000000000011",
        .activation_clicks = 5,
    },
    {
        .id = DEVICE_ROW_FACTORY_RESET,
        .title = "恢复出厂设置",
        .kind = SETTINGS_PAGE_ROW_ACTION,
    },
    {
        .id = DEVICE_ROW_MOBILE_QRCODE,
        .title = "移动爱家二维码",
        .kind = SETTINGS_PAGE_ROW_ACTION,
    },
    {
        .id = DEVICE_ROW_SERVICE,
        .title = "用户服务协议",
        .kind = SETTINGS_PAGE_ROW_ACTION,
    },
    {
        .id = DEVICE_ROW_PRIVACY,
        .title = "隐私政策",
        .kind = SETTINGS_PAGE_ROW_ACTION,
    },
    {
        .id = DEVICE_ROW_USER_RULE,
        .title = "儿童个人信息保护规则及监护人须知",
        .kind = SETTINGS_PAGE_ROW_ACTION,
    },
    {
        .id = DEVICE_ROW_AIGC,
        .title = "AIGC使用规则",
        .kind = SETTINGS_PAGE_ROW_ACTION,
    },
};

static const settings_page_row_t upgrade_rows[] = {
    {
        .id = UPGRADE_ROW_START,
        .title = "立即更新",
        .kind = SETTINGS_PAGE_ROW_ACTION,
    },
};

static void settings_demo_create_page(
    settings_demo_t *demo,
    const char *title,
    settings_page_layout_t layout,
    const settings_page_row_t *rows,
    uint8_t row_count,
    bool show_back,
    settings_page_variant_t variant);
static void settings_demo_apply_view(void *user_ctx);

static void settings_demo_navigate(
    settings_demo_t *demo, settings_demo_view_t view)
{
    demo->pending_view = (uint8_t)view;
    if(demo->navigation_pending) return;
    demo->navigation_pending = true;
    if(lv_async_call(settings_demo_apply_view, demo) != LV_RESULT_OK) {
        demo->navigation_pending = false;
    }
}

static void settings_demo_push(
    settings_demo_t *demo, settings_demo_view_t view)
{
    if(demo->view_depth < sizeof(demo->view_stack)) {
        demo->view_stack[demo->view_depth++] = demo->current_view;
    }
    settings_demo_navigate(demo, view);
}

static void settings_demo_pop(settings_demo_t *demo)
{
    settings_demo_view_t view = SETTINGS_DEMO_OVERVIEW;

    if(demo->view_depth > 0u) {
        view = (settings_demo_view_t)
            demo->view_stack[--demo->view_depth];
    }
    settings_demo_navigate(demo, view);
}

static void settings_demo_on_event(
    void *user_ctx, const settings_page_event_t *event)
{
    settings_demo_t *demo = user_ctx;

    ++demo->event_count;
    demo->last_row_id = event->row_id;
    demo->last_value = event->value;
    demo->last_event_kind = event->kind;
    demo->last_click_count = event->click_count;
    if(event->kind == SETTINGS_PAGE_EVENT_BACK) {
        if(!demo->showing_overview) {
            settings_demo_pop(demo);
        }
        return;
    }
    if(demo->showing_overview &&
       event->kind == SETTINGS_PAGE_EVENT_ROW_ACTIVATED) {
        switch(event->row_id) {
            case SETTINGS_ROW_WIFI:
                settings_demo_push(demo, SETTINGS_DEMO_WIFI);
                break;
            case SETTINGS_ROW_DISPLAY:
                settings_demo_push(demo, SETTINGS_DEMO_DISPLAY);
                break;
            case SETTINGS_ROW_VOLUME:
                settings_demo_push(demo, SETTINGS_DEMO_VOLUME);
                break;
            case SETTINGS_ROW_ABOUT:
                settings_demo_push(demo, SETTINGS_DEMO_ABOUT);
                break;
            case SETTINGS_ROW_MOBILE:
                settings_demo_push(demo, SETTINGS_DEMO_MOBILE);
                break;
            case SETTINGS_ROW_STORAGE:
                settings_demo_push(demo, SETTINGS_DEMO_STORAGE);
                break;
            default:
                break;
        }
        return;
    }
    if(event->kind == SETTINGS_PAGE_EVENT_ROW_ACTIVATED &&
       event->row_id == ABOUT_ROW_DEVICE) {
        settings_demo_push(demo, SETTINGS_DEMO_ABOUT_DEVICE);
        return;
    }
    if(event->kind == SETTINGS_PAGE_EVENT_ROW_ACTIVATED &&
       event->row_id == ABOUT_ROW_UPDATE) {
        settings_demo_push(demo, SETTINGS_DEMO_UPGRADE);
        return;
    }
    if(event->kind == SETTINGS_PAGE_EVENT_ROW_ACTIVATED &&
       (event->row_id == DEVICE_ROW_MODEL ||
        event->row_id == DEVICE_ROW_SN ||
        event->row_id == DEVICE_ROW_IMEI ||
        event->row_id == DEVICE_ROW_CMEI)) {
        settings_demo_show_status(demo, "隐藏入口已触发");
        return;
    }
    if(event->kind == SETTINGS_PAGE_EVENT_SWITCH_CHANGED) {
        if(event->row_id == WIFI_ROW_ENABLED && demo->page != NULL) {
            (void)settings_page_set_wifi_loading(
                demo->page, event->value != 0);
        }
        settings_demo_show_status(
            demo,
            event->value != 0 ? "开关已开启" : "开关已关闭");
    }
    else if(event->kind == SETTINGS_PAGE_EVENT_SLIDER_CHANGED) {
        char message[64];

        (void)snprintf(
            message, sizeof(message), "当前值：%ld", (long)event->value);
        settings_demo_show_status(demo, message);
    }
    else if(event->kind == SETTINGS_PAGE_EVENT_CHOICE_CHANGED) {
        settings_demo_show_status(demo, "选项已更新");
    }
    else if(event->kind == SETTINGS_PAGE_EVENT_ROW_ACTIVATED) {
        settings_demo_show_status(demo, "操作已触发（模拟）");
    }
}

static void settings_demo_create_feedback(settings_demo_t *demo)
{
    demo->feedback = lv_label_create(lv_screen_active());
    lv_obj_set_size(demo->feedback, 360, 34);
    lv_obj_align(demo->feedback, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_radius(demo->feedback, 8, 0);
    lv_obj_set_style_bg_color(demo->feedback, lv_color_hex(0x232323), 0);
    lv_obj_set_style_bg_opa(demo->feedback, LV_OPA_90, 0);
    lv_obj_set_style_pad_hor(demo->feedback, 12, 0);
    lv_obj_set_style_pad_ver(demo->feedback, 4, 0);
    lv_obj_set_style_text_align(demo->feedback, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(demo->feedback, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(demo->feedback, &settings_demo_cn_20, 0);
    lv_label_set_text(demo->feedback, "");
    lv_obj_add_flag(demo->feedback, LV_OBJ_FLAG_HIDDEN);
}

static bool settings_demo_init_display(settings_demo_t *demo)
{
    SDL_Renderer *renderer;
    SDL_Window *window;
    lv_indev_t *mouse;
    lv_indev_t *mousewheel;
    lv_indev_t *keyboard;

    memset(demo, 0, sizeof(*demo));
    demo->display =
        lv_sdl_window_create(SETTINGS_DEMO_WIDTH, SETTINGS_DEMO_HEIGHT);
    if(demo->display == NULL) return false;
    lv_sdl_window_set_title(demo->display, "Settings page");
    lv_display_set_default(demo->display);
    renderer = lv_sdl_window_get_renderer(demo->display);
    window = renderer != NULL ? SDL_RenderGetWindow(renderer) : NULL;
    if(window == NULL) return false;
    demo->window_id = SDL_GetWindowID(window);
    mouse = lv_sdl_mouse_create();
    mousewheel = lv_sdl_mousewheel_create();
    keyboard = lv_sdl_keyboard_create();
    if(mouse == NULL || mousewheel == NULL || keyboard == NULL) return false;
    lv_indev_set_display(mouse, demo->display);
    lv_indev_set_display(mousewheel, demo->display);
    lv_indev_set_display(keyboard, demo->display);
    lv_obj_set_style_bg_color(
        lv_screen_active(), lv_color_hex(0x090B0F), 0);
    demo->content_host = lv_obj_create(lv_screen_active());
    if(demo->content_host == NULL) return false;
    lv_obj_remove_style_all(demo->content_host);
    lv_obj_set_size(
        demo->content_host, SETTINGS_DEMO_WIDTH, SETTINGS_DEMO_HEIGHT - 60);
    lv_obj_set_pos(demo->content_host, 0, 60);
    lv_obj_set_style_bg_color(
        demo->content_host, lv_color_hex(0x090B0F), 0);
    lv_obj_set_style_bg_opa(demo->content_host, LV_OPA_COVER, 0);
    lv_obj_clear_flag(demo->content_host, LV_OBJ_FLAG_CLICKABLE);
    if(settings_demo_create_status_bar(demo) == NULL) return false;
    return true;
}

static void settings_demo_create_page(
    settings_demo_t *demo,
    const char *title,
    settings_page_layout_t layout,
    const settings_page_row_t *rows,
    uint8_t row_count,
    bool show_back,
    settings_page_variant_t variant)
{
    const settings_page_assets_t *assets =
        variant == SETTINGS_PAGE_VARIANT_UNIGUI_BRIGHTNESS
            ? &brightness_assets
            : &unigui_assets;
    settings_page_config_t config = {
        .title = title,
        .layout = layout,
        .rows = rows,
        .row_count = row_count,
        .show_back = show_back,
        .header_height = 56u,
        .symbol_font = &lv_font_montserrat_32,
        .title_font = &settings_demo_product_font,
        .overview_font = &SourceHanSansCN_Medium_32px_4bpp,
        .section_font = &SourceHanSansCN_Regular_16px_4bpp,
        .emphasis_font =
            (variant == SETTINGS_PAGE_VARIANT_UNIGUI_STORAGE ||
             variant == SETTINGS_PAGE_VARIANT_UNIGUI_WIFI)
                ? &ui_font_MED36
                : &settings_demo_product_font,
        .body_font =
            variant == SETTINGS_PAGE_VARIANT_UNIGUI_ABOUT_DEVICE
                ? &SourceHanSansCN_Regular_32px_4bpp
                : (variant == SETTINGS_PAGE_VARIANT_UNIGUI_BRIGHTNESS ||
                           variant == SETTINGS_PAGE_VARIANT_UNIGUI_WIFI
                       ? &SourceHanSansCN_Regular_24px_4bpp
                       : (variant == SETTINGS_PAGE_VARIANT_UNIGUI_STORAGE
                              ? &settings_demo_body_font
                              : (variant == SETTINGS_PAGE_VARIANT_DEFAULT
                                     ? &settings_demo_product_font
                                     : &settings_demo_product_font))),
        .theme = NULL,
        .on_event = settings_demo_on_event,
        .user_ctx = demo,
        .style = SETTINGS_PAGE_STYLE_UNIGUI,
        .variant = variant,
        .assets = assets,
    };

    if(demo->page != NULL) settings_page_destroy(demo->page);
    demo->page = settings_page_create(demo->content_host, &config);
    demo->showing_overview =
        variant == SETTINGS_PAGE_VARIANT_DEFAULT &&
        layout == SETTINGS_PAGE_LAYOUT_GRID;
    if(demo->status_bar != NULL) lv_obj_move_foreground(demo->status_bar);
    if(demo->feedback != NULL) lv_obj_move_foreground(demo->feedback);
}

static void settings_demo_apply_view(void *user_ctx)
{
    settings_demo_t *demo = user_ctx;

    demo->navigation_pending = false;
    demo->current_view = demo->pending_view;
    switch((settings_demo_view_t)demo->pending_view) {
        case SETTINGS_DEMO_WIFI:
            settings_demo_create_page(
                demo, "Wi-Fi", SETTINGS_PAGE_LAYOUT_LIST, wifi_rows,
                (uint8_t)(sizeof(wifi_rows) / sizeof(wifi_rows[0])), true,
                SETTINGS_PAGE_VARIANT_UNIGUI_WIFI);
            break;
        case SETTINGS_DEMO_MOBILE:
            settings_demo_create_page(
                demo, "移动网络", SETTINGS_PAGE_LAYOUT_LIST, mobile_rows,
                (uint8_t)(sizeof(mobile_rows) / sizeof(mobile_rows[0])), true,
                SETTINGS_PAGE_VARIANT_UNIGUI_MOBILE_NETWORK);
            break;
        case SETTINGS_DEMO_DISPLAY:
            settings_demo_create_page(
                demo, "显示", SETTINGS_PAGE_LAYOUT_LIST, display_rows,
                (uint8_t)(sizeof(display_rows) / sizeof(display_rows[0])), true,
                SETTINGS_PAGE_VARIANT_UNIGUI_BRIGHTNESS);
            break;
        case SETTINGS_DEMO_VOLUME:
            settings_demo_create_page(
                demo, "音量", SETTINGS_PAGE_LAYOUT_LIST, volume_rows,
                (uint8_t)(sizeof(volume_rows) / sizeof(volume_rows[0])), true,
                SETTINGS_PAGE_VARIANT_UNIGUI_VOLUME);
            break;
        case SETTINGS_DEMO_STORAGE:
            settings_demo_create_page(
                demo, "存储", SETTINGS_PAGE_LAYOUT_LIST, storage_rows,
                (uint8_t)(sizeof(storage_rows) / sizeof(storage_rows[0])), true,
                SETTINGS_PAGE_VARIANT_UNIGUI_STORAGE);
            break;
        case SETTINGS_DEMO_ABOUT:
            settings_demo_create_page(
                demo, "关于", SETTINGS_PAGE_LAYOUT_LIST, about_rows,
                (uint8_t)(sizeof(about_rows) / sizeof(about_rows[0])), true,
                SETTINGS_PAGE_VARIANT_UNIGUI_ABOUT);
            break;
        case SETTINGS_DEMO_ABOUT_DEVICE:
            settings_demo_create_page(
                demo, "关于本机", SETTINGS_PAGE_LAYOUT_LIST,
                about_device_rows,
                (uint8_t)(sizeof(about_device_rows) /
                          sizeof(about_device_rows[0])),
                true,
                SETTINGS_PAGE_VARIANT_UNIGUI_ABOUT_DEVICE);
            break;
        case SETTINGS_DEMO_UPGRADE:
            settings_demo_create_page(
                demo, "系统升级", SETTINGS_PAGE_LAYOUT_LIST,
                upgrade_rows,
                (uint8_t)(sizeof(upgrade_rows) / sizeof(upgrade_rows[0])),
                true,
                SETTINGS_PAGE_VARIANT_UNIGUI_UPGRADE);
            break;
        case SETTINGS_DEMO_OVERVIEW:
        default:
            settings_demo_create_page(
                demo,
                "设置",
                SETTINGS_PAGE_LAYOUT_GRID,
                overview_rows,
                (uint8_t)(sizeof(overview_rows) / sizeof(overview_rows[0])),
                true,
                SETTINGS_PAGE_VARIANT_DEFAULT);
            break;
    }
}

static bool settings_demo_expect(
    const char *stage, bool condition)
{
    if(condition) return true;
    fprintf(stderr, "settings smoke failed at %s\n", stage);
    return false;
}

static lv_obj_t *settings_demo_find_wifi_surface(lv_obj_t *parent)
{
    uint32_t child_count;

    if(parent == NULL) return NULL;
    child_count = lv_obj_get_child_count(parent);
    for(uint32_t index = 0u; index < child_count; ++index) {
        lv_obj_t *child = lv_obj_get_child(parent, (int32_t)index);
        lv_obj_t *match;

        if(child == NULL) continue;
        if(lv_obj_get_width(child) == 620 &&
           lv_obj_get_height(child) == 353 &&
           lv_obj_has_flag(child, LV_OBJ_FLAG_SCROLLABLE)) {
            return child;
        }
        match = settings_demo_find_wifi_surface(child);
        if(match != NULL) return match;
    }
    return NULL;
}

static int settings_demo_run_smoke(settings_demo_t *demo)
{
    uint32_t events_before;
    lv_obj_t *wifi_surface;
    lv_obj_t *row_object;
    settings_page_wifi_state_t wifi_state;
    uint32_t key;
    int32_t scroll_before;

    if(!settings_demo_expect(
           "initial page", demo->page != NULL && demo->showing_overview)) {
        return 1;
    }
    if(!settings_demo_click(demo, 190, 200)) return 1;
    if(!settings_demo_expect(
           "open wifi", demo->page != NULL && !demo->showing_overview)) {
        return 1;
    }
    wifi_surface = settings_demo_find_wifi_surface(settings_page_root(demo->page));
    if(!settings_demo_expect("wifi scroll surface", wifi_surface != NULL)) {
        return 1;
    }
    scroll_before = lv_obj_get_scroll_y(wifi_surface);
    lv_obj_scroll_by(wifi_surface, 0, 40, LV_ANIM_OFF);
    settings_demo_pump(demo, 20u);
    if(!settings_demo_expect(
           "wifi scroll", lv_obj_get_scroll_y(wifi_surface) != scroll_before)) {
        return 1;
    }
    wifi_state.encrypted = true;
    wifi_state.remembered = false;
    wifi_state.signal_strength = -95;
    wifi_state.status = SETTINGS_PAGE_WIFI_STATUS_ERROR;
    if(!settings_demo_expect(
           "wifi state update",
           settings_page_set_wifi_state(demo->page, 104u, &wifi_state))) {
        return 1;
    }
    if(!settings_demo_expect(
           "wifi loading on",
           settings_page_set_wifi_loading(demo->page, true))) {
        return 1;
    }
    if(!settings_demo_expect(
           "wifi loading off",
           settings_page_set_wifi_loading(demo->page, false))) {
        return 1;
    }
    lv_obj_scroll_to_y(wifi_surface, 0, LV_ANIM_OFF);
    settings_demo_pump(demo, 20u);
    if(!settings_demo_click(demo, 300, 245)) return 1;
    if(!settings_demo_expect(
           "wifi network", demo->last_row_id == WIFI_ROW_NETWORK)) {
        return 1;
    }
    events_before = demo->event_count;
    if(!settings_demo_click(demo, 300, 355)) return 1;
    if(!settings_demo_expect(
           "wifi network action", demo->last_row_id == WIFI_ROW_ADD)) {
        return 1;
    }
    events_before = demo->event_count;
    if(!settings_demo_long_press(demo, 300, 355)) return 1;
    if(!settings_demo_expect(
           "wifi long press", demo->last_row_id == WIFI_ROW_ADD &&
               demo->last_event_kind == SETTINGS_PAGE_EVENT_ROW_LONG_PRESSED &&
               demo->event_count == events_before + 1u)) {
        return 1;
    }
    events_before = demo->event_count;
    if(!settings_demo_click(demo, 650, 180)) return 1;
    if(!settings_demo_expect(
           "wifi switch", demo->last_row_id == WIFI_ROW_ENABLED &&
               demo->event_count == events_before + 1u)) {
        return 1;
    }
    events_before = demo->event_count;
    if(!settings_demo_click(demo, 300, 245)) return 1;
    if(!settings_demo_expect(
           "wifi disabled network", demo->event_count == events_before)) {
        return 1;
    }
    if(!settings_demo_expect(
           "wifi reenable",
           settings_page_set_checked(demo->page, WIFI_ROW_ENABLED, true))) {
        return 1;
    }
    if(!settings_demo_click(demo, 60, 108)) return 1;
    if(!settings_demo_expect(
           "return", demo->showing_overview && demo->page != NULL)) {
        return 1;
    }
    row_object = settings_page_row_object(demo->page, SETTINGS_ROW_VOLUME);
    if(!settings_demo_expect("volume row object", row_object != NULL)) {
        return 1;
    }
    events_before = demo->event_count;
    (void)lv_obj_send_event(row_object, LV_EVENT_CLICKED, NULL);
    settings_demo_pump(demo, 30u);
    if(!settings_demo_expect(
           "programmatic volume navigation",
           !demo->showing_overview && demo->last_row_id == SETTINGS_ROW_VOLUME)) {
        return 1;
    }
    if(!settings_demo_click(demo, 60, 108)) return 1;
    if(!settings_demo_expect("volume return", demo->showing_overview)) {
        return 1;
    }
    if(!settings_demo_click(demo, 410, 400)) return 1;
    if(!settings_demo_expect(
           "open storage", !demo->showing_overview && demo->page != NULL)) {
        return 1;
    }
    if(!settings_demo_click(demo, 60, 108)) return 1;
    if(!settings_demo_expect("storage return", demo->showing_overview)) {
        return 1;
    }
    settings_demo_create_page(
        demo,
        "显示",
        SETTINGS_PAGE_LAYOUT_LIST,
        display_rows,
        (uint8_t)(sizeof(display_rows) / sizeof(display_rows[0])),
        true,
        SETTINGS_PAGE_VARIANT_UNIGUI_BRIGHTNESS);
    settings_demo_pump(demo, 20u);
    if(!settings_demo_expect(
           "display page", demo->page != NULL && !demo->showing_overview)) {
        return 1;
    }
    if(!settings_demo_expect(
           "set brightness",
           settings_page_set_value(demo->page, DISPLAY_ROW_BRIGHTNESS, 73))) {
        return 1;
    }
    if(!settings_demo_expect(
           "set screen off choice",
           settings_page_set_choice(demo->page, DISPLAY_ROW_SCREEN_OFF, 2u))) {
        return 1;
    }
    if(!settings_demo_expect(
           "set shutdown choice",
           settings_page_set_choice(demo->page, DISPLAY_ROW_SHUTDOWN, 3u))) {
        return 1;
    }
    if(!settings_demo_expect(
           "disable shutdown row",
           settings_page_set_enabled(demo->page, DISPLAY_ROW_SHUTDOWN, false))) {
        return 1;
    }
    settings_page_set_title(demo->page, "显示设置");
    if(!settings_page_set_enabled(demo->page, DISPLAY_ROW_BRIGHTNESS, false)) {
        return 1;
    }
    events_before = demo->event_count;
    if(!settings_demo_click(demo, 650, 170)) return 1;
    if(!settings_demo_expect(
           "disabled brightness", demo->event_count == events_before)) {
        return 1;
    }
    if(!settings_page_set_enabled(demo->page, DISPLAY_ROW_BRIGHTNESS, true)) {
        return 1;
    }
    if(!settings_demo_click(demo, 650, 170)) return 1;
    if(!settings_demo_expect(
           "brightness slider", demo->last_row_id == DISPLAY_ROW_BRIGHTNESS)) {
        return 1;
    }
    if(!settings_demo_expect(
           "brightness step", demo->last_value == 93)) {
        return 1;
    }
    if(!settings_demo_click(demo, 60, 108)) return 1;
    if(!settings_demo_expect(
           "display return", demo->showing_overview && demo->page != NULL)) {
        return 1;
    }
    if(!settings_demo_click(demo, 615, 400)) return 1;
    if(!settings_demo_expect(
           "open about", demo->page != NULL && !demo->showing_overview)) {
        return 1;
    }
    events_before = demo->event_count;
    if(!settings_demo_click(demo, 400, 170)) return 1;
    if(!settings_demo_expect(
           "about update action", demo->last_row_id == ABOUT_ROW_UPDATE &&
               demo->last_event_kind == SETTINGS_PAGE_EVENT_ROW_ACTIVATED &&
               demo->event_count == events_before + 1u)) {
        return 1;
    }
    if(!settings_demo_expect(
           "open upgrade", demo->current_view == SETTINGS_DEMO_UPGRADE)) {
        return 1;
    }
    if(!settings_demo_click(demo, 60, 108)) return 1;
    if(!settings_demo_expect(
           "upgrade return", demo->current_view == SETTINGS_DEMO_ABOUT)) {
        return 1;
    }
    if(!settings_demo_click(demo, 400, 260)) return 1;
    if(!settings_demo_expect(
           "open about device", demo->last_row_id == ABOUT_ROW_DEVICE)) {
        return 1;
    }
    events_before = demo->event_count;
    for(uint8_t click_index = 0u; click_index < 5u; ++click_index) {
        if(!settings_demo_click(demo, 300, 160)) return 1;
    }
    if(!settings_demo_expect(
           "about device multi click",
           demo->last_row_id == DEVICE_ROW_MODEL &&
               demo->last_event_kind == SETTINGS_PAGE_EVENT_ROW_ACTIVATED &&
               demo->last_click_count == 5u &&
               demo->event_count == events_before + 1u)) {
        return 1;
    }
    key = LV_KEY_ESC;
    row_object = settings_page_row_object(demo->page, DEVICE_ROW_MODEL);
    if(!settings_demo_expect("about device row object", row_object != NULL)) {
        return 1;
    }
    (void)lv_obj_send_event(row_object, LV_EVENT_KEY, &key);
    settings_demo_pump(demo, 30u);
    if(!settings_demo_expect(
           "keyboard escape", demo->current_view == SETTINGS_DEMO_ABOUT &&
               demo->page != NULL)) {
        return 1;
    }
    if(!settings_demo_click(demo, 60, 108)) return 1;
    if(!settings_demo_expect(
           "about return", demo->showing_overview && demo->page != NULL)) {
        return 1;
    }
    if(!settings_demo_click(demo, 400, 200)) return 1;
    if(!settings_demo_expect(
           "open mobile", demo->page != NULL && !demo->showing_overview)) {
        return 1;
    }
    if(!settings_demo_expect(
           "reject invalid rows",
           !settings_page_set_rows(demo->page, NULL, 1u))) {
        return 1;
    }
    if(!settings_demo_expect(
           "rollback row object",
           settings_page_row_object(demo->page, 502u) != NULL)) {
        return 1;
    }
    if(!settings_demo_expect(
           "replace mobile rows",
           settings_page_set_rows(
               demo->page,
               mobile_rows_refreshed,
               (uint8_t)(sizeof(mobile_rows_refreshed) /
                         sizeof(mobile_rows_refreshed[0]))))) {
        return 1;
    }
    events_before = demo->event_count;
    if(!settings_demo_click(demo, 650, 170)) return 1;
    if(!settings_demo_expect(
           "mobile switch", demo->last_row_id == 502u &&
               demo->event_count == events_before + 1u)) {
        return 1;
    }
    printf("settings page smoke passed\n");
    return 0;
}

static int settings_demo_write_screenshots(
    settings_demo_t *demo, const char *directory)
{
    char path[512];
    int written;

    written = snprintf(
        path, sizeof(path), "%s/settings-overview.bmp", directory);
    if(written < 0 || (size_t)written >= sizeof(path) ||
       !settings_demo_write_snapshot(demo, path)) {
        return 1;
    }
    {
        const struct {
            settings_demo_view_t view;
            const char *name;
        } views[] = {
            {SETTINGS_DEMO_DISPLAY, "settings-display.bmp"},
            {SETTINGS_DEMO_VOLUME, "settings-volume.bmp"},
            {SETTINGS_DEMO_STORAGE, "settings-storage.bmp"},
            {SETTINGS_DEMO_MOBILE, "settings-mobile-network.bmp"},
            {SETTINGS_DEMO_ABOUT, "settings-about.bmp"},
            {SETTINGS_DEMO_ABOUT_DEVICE, "settings-about-device.bmp"},
            {SETTINGS_DEMO_WIFI, "settings-wifi.bmp"},
        };
        size_t view_index;

        for(view_index = 0u;
            view_index < sizeof(views) / sizeof(views[0]);
            ++view_index) {
            settings_demo_navigate(demo, views[view_index].view);
            settings_demo_pump(demo, 250u);
            written = snprintf(
                path, sizeof(path), "%s/%s", directory,
                views[view_index].name);
            if(written < 0 || (size_t)written >= sizeof(path) ||
               !settings_demo_write_snapshot(demo, path)) {
                return 1;
            }
        }
    }
    printf("settings page screenshots written to %s\n", directory);
    return 0;
}

int main(int argc, char **argv)
{
    bool run_smoke = argc == 2 && strcmp(argv[1], "--smoke") == 0;
    bool write_screenshots =
        argc == 3 && strcmp(argv[1], "--screenshots") == 0;
    settings_demo_t demo;
    lv_obj_t *screen;

    if(argc > 3 || (argc == 2 && !run_smoke) ||
       (argc == 3 && !write_screenshots)) {
        fprintf(
            stderr,
            "usage: settings_page_demo [--smoke | --screenshots DIR]\n");
        return 2;
    }
    lv_init();
    settings_demo_product_font = SourceHanSansCN_Medium_28px_4bpp;
    settings_demo_product_font.fallback = &ui_font_MED36;
    settings_demo_body_font = SourceHanSansCN_Medium_24px_4bpp;
    settings_demo_body_font.fallback = &SourceHanSansCN_Regular_24px_4bpp;
    if(!settings_demo_init_display(&demo)) return 1;
    screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x090B0F), 0);
    settings_demo_create_page(
        &demo,
        "设置",
        SETTINGS_PAGE_LAYOUT_GRID,
        overview_rows,
        (uint8_t)(sizeof(overview_rows) / sizeof(overview_rows[0])),
        true,
        SETTINGS_PAGE_VARIANT_DEFAULT);
    settings_demo_create_feedback(&demo);
    settings_demo_pump(&demo, 20u);
    if(run_smoke) return settings_demo_run_smoke(&demo);
    if(write_screenshots) {
        return settings_demo_write_screenshots(&demo, argv[2]);
    }
    while(true) {
        (void)lv_timer_handler();
        SDL_Delay(5u);
    }
}
