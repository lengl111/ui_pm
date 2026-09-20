#include "photo_gallery/photo_gallery.h"

#include <stddef.h>
#include <string.h>

#define PHOTO_GALLERY_DEFAULT_COLUMNS 3u
#define PHOTO_GALLERY_DEFAULT_ROWS 2u
#define PHOTO_GALLERY_DEFAULT_CACHE 18u
#define PHOTO_GALLERY_DEFAULT_PREFETCH 3u
#define PHOTO_GALLERY_DEFAULT_THUMB_WIDTH 230u
#define PHOTO_GALLERY_DEFAULT_THUMB_HEIGHT 140u
#define PHOTO_GALLERY_DEFAULT_GAP_X 12u
#define PHOTO_GALLERY_DEFAULT_GAP_Y 14u
#define PHOTO_GALLERY_DEFAULT_PADDING_LEFT 20u
#define PHOTO_GALLERY_DEFAULT_PADDING_RIGHT 20u
#define PHOTO_GALLERY_DEFAULT_PADDING_TOP 68u
#define PHOTO_GALLERY_DEFAULT_PADDING_BOTTOM 54u
#define PHOTO_GALLERY_DEFAULT_SWIPE_THRESHOLD 64u
#define PHOTO_GALLERY_INVALID_INDEX UINT32_MAX
#define PHOTO_GALLERY_FLICK_VELOCITY 5
#define PHOTO_GALLERY_RELEASE_THRESHOLD_PER_MILLE 200

typedef struct {
    struct photo_gallery *gallery;
    lv_obj_t *root;
    lv_obj_t *image;
    lv_obj_t *title;
    lv_obj_t *checked;
    uint32_t index;
    uint32_t last_used;
} photo_gallery_cache_item_t;

struct photo_gallery {
    lv_obj_t *root;
    lv_obj_t *list_view;
    lv_obj_t *scroll_extent;
    lv_obj_t *empty_label;
    lv_obj_t *title_label;
    lv_obj_t *page_label;
    lv_obj_t *viewer_root;
    lv_obj_t *viewer_image;
    lv_obj_t *viewer_title;
    lv_obj_t *viewer_index_label;
    lv_obj_t *viewer_close;
    lv_obj_t *viewer_previous;
    lv_obj_t *viewer_next;
    photo_gallery_cache_item_t cache[PHOTO_GALLERY_MAX_CACHE_ITEMS];
    const photo_gallery_item_t *items;
    photo_gallery_item_provider_fn item_provider;
    photo_gallery_range_request_fn request_range;
    photo_gallery_event_fn on_event;
    void *user_ctx;
    uint8_t *selected;
    uint32_t item_count;
    uint32_t selected_count;
    uint8_t columns;
    uint8_t rows_per_page;
    uint8_t cache_size;
    uint16_t prefetch_items;
    uint16_t thumbnail_width;
    uint16_t thumbnail_height;
    uint16_t gap_x;
    uint16_t gap_y;
    uint16_t padding_left;
    uint16_t padding_right;
    uint16_t base_padding_left;
    uint16_t base_padding_right;
    uint16_t padding_top;
    uint16_t padding_bottom;
    const lv_font_t *title_font;
    const lv_font_t *body_font;
    uint16_t viewer_swipe_threshold;
    const char *title;
    bool snap_to_page;
    bool selection_mode;
    bool viewer_open;
    bool viewer_dragging;
    bool geometry_valid;
    bool scroll_active;
    bool programmatic_scroll;
    bool release_page_pending;
    bool suppress_scroll_end;
    bool correcting_scroll;
    bool request_valid;
    bool layout_refreshing;
    bool building;
    uint32_t request_from;
    uint32_t request_to;
    uint32_t current_page;
    uint32_t gesture_start_page;
    uint32_t release_page;
    uint32_t target_page;
    uint32_t viewer_index;
    uint32_t viewer_id;
    uint32_t use_clock;
    int32_t page_height;
    int32_t scroll_max;
    int32_t viewer_press_x;
};

static void photo_gallery_refresh_layout(photo_gallery_t *gallery);
static void photo_gallery_refresh_visible(photo_gallery_t *gallery);
static bool photo_gallery_set_viewer_index_internal(
    photo_gallery_t *gallery, uint32_t index, bool emit_event);

static uint32_t photo_gallery_page_count_for(
    const photo_gallery_t *gallery)
{
    uint32_t items_per_page;

    if(gallery == NULL || gallery->item_count == 0u) return 0u;
    items_per_page = (uint32_t)gallery->columns * gallery->rows_per_page;
    if(items_per_page == 0u) return 0u;
    return gallery->item_count / items_per_page +
           (gallery->item_count % items_per_page != 0u ? 1u : 0u);
}

static uint32_t photo_gallery_items_per_page(
    const photo_gallery_t *gallery)
{
    return (uint32_t)gallery->columns * gallery->rows_per_page;
}

static uint32_t photo_gallery_clamp_page(
    const photo_gallery_t *gallery, uint32_t page_index)
{
    uint32_t page_count = photo_gallery_page_count_for(gallery);

    if(page_count == 0u) return 0u;
    return page_index < page_count ? page_index : page_count - 1u;
}

static const lv_font_t *photo_gallery_font_or_default(
    const lv_font_t *font)
{
    return font != NULL ? font : LV_FONT_DEFAULT;
}

static void photo_gallery_make_plain(lv_obj_t *object)
{
    lv_obj_remove_style_all(object);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

static void photo_gallery_emit(
    photo_gallery_t *gallery,
    photo_gallery_event_kind_t kind,
    uint32_t index,
    uint32_t id,
    uint32_t page_index,
    lv_dir_t direction,
    bool selected)
{
    photo_gallery_event_t event;

    if(gallery == NULL || gallery->on_event == NULL) return;
    event.kind = kind;
    event.index = index;
    event.id = id;
    event.page_index = page_index;
    event.page_count = photo_gallery_page_count_for(gallery);
    event.selected_count = gallery->selected_count;
    event.selected = selected;
    event.direction = direction;
    gallery->on_event(gallery->user_ctx, &event);
}

static bool photo_gallery_get_item(
    const photo_gallery_t *gallery,
    uint32_t index,
    photo_gallery_item_t *item)
{
    if(gallery == NULL || item == NULL || index >= gallery->item_count) {
        return false;
    }
    memset(item, 0, sizeof(*item));
    if(gallery->item_provider != NULL) {
        return gallery->item_provider(gallery->user_ctx, index, item);
    }
    if(gallery->items == NULL) return false;
    *item = gallery->items[index];
    return true;
}

static void photo_gallery_clear_cache_item(
    photo_gallery_cache_item_t *cache_item)
{
    if(cache_item == NULL || cache_item->root == NULL) return;
    cache_item->index = PHOTO_GALLERY_INVALID_INDEX;
    cache_item->last_used = 0u;
    lv_image_set_src(cache_item->image, NULL);
    lv_label_set_text(cache_item->title, "");
    lv_obj_add_flag(cache_item->root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cache_item->checked, LV_OBJ_FLAG_HIDDEN);
}

static bool photo_gallery_render_cache_item(
    photo_gallery_t *gallery,
    photo_gallery_cache_item_t *cache_item,
    uint32_t index,
    bool visible)
{
    photo_gallery_item_t item;
    bool available;
    const void *image_src;

    if(gallery == NULL || cache_item == NULL) return false;
    available = photo_gallery_get_item(gallery, index, &item);
    cache_item->index = index;
    cache_item->last_used = ++gallery->use_clock;
    image_src = available
                    ? (item.thumbnail_src != NULL
                           ? item.thumbnail_src
                           : item.image_src)
                    : NULL;
    lv_image_set_src(cache_item->image, image_src);
    if(available) {
        lv_label_set_text(
            cache_item->title,
            item.title != NULL ? item.title : "");
        if(item.disabled) {
            lv_obj_add_state(cache_item->root, LV_STATE_DISABLED);
        }
        else {
            lv_obj_clear_state(cache_item->root, LV_STATE_DISABLED);
        }
    }
    else {
        lv_label_set_text(cache_item->title, "Loading");
        lv_obj_clear_state(cache_item->root, LV_STATE_DISABLED);
    }
    if(gallery->selection_mode &&
       index < gallery->item_count && gallery->selected != NULL &&
       gallery->selected[index] != 0u) {
        lv_obj_remove_flag(cache_item->checked, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(cache_item->checked, LV_OBJ_FLAG_HIDDEN);
    }
    if(visible) {
        lv_obj_remove_flag(cache_item->root, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(cache_item->root, LV_OBJ_FLAG_HIDDEN);
    }
    return available;
}

static int32_t photo_gallery_find_cache_slot(
    const photo_gallery_t *gallery, uint32_t index)
{
    for(uint8_t slot = 0u; slot < gallery->cache_size; ++slot) {
        if(gallery->cache[slot].index == index) return (int32_t)slot;
    }
    return -1;
}

static int32_t photo_gallery_choose_cache_slot(
    const photo_gallery_t *gallery, const bool *claimed)
{
    int32_t selected_slot = -1;
    uint32_t oldest_use = UINT32_MAX;

    for(uint8_t slot = 0u; slot < gallery->cache_size; ++slot) {
        if(claimed[slot]) continue;
        if(gallery->cache[slot].index == PHOTO_GALLERY_INVALID_INDEX) {
            return (int32_t)slot;
        }
        if(gallery->cache[slot].last_used < oldest_use) {
            oldest_use = gallery->cache[slot].last_used;
            selected_slot = (int32_t)slot;
        }
    }
    return selected_slot;
}

static void photo_gallery_update_page_label(photo_gallery_t *gallery)
{
    uint32_t page_count;

    if(gallery == NULL || gallery->page_label == NULL) return;
    page_count = photo_gallery_page_count_for(gallery);
    if(page_count == 0u) {
        lv_label_set_text(gallery->page_label, "0 / 0");
    }
    else {
        lv_label_set_text_fmt(
            gallery->page_label,
            "%u / %u",
            (unsigned)(gallery->current_page + 1u),
            (unsigned)page_count);
    }
}

static int32_t photo_gallery_current_scroll_y(
    const photo_gallery_t *gallery)
{
    int32_t scroll_y;

    if(gallery == NULL || gallery->list_view == NULL) return 0;
    scroll_y = lv_obj_get_scroll_y(gallery->list_view);
    if(scroll_y < 0) return 0;
    if(scroll_y > gallery->scroll_max) return gallery->scroll_max;
    return scroll_y;
}

static uint32_t photo_gallery_nearest_page(
    const photo_gallery_t *gallery)
{
    int32_t scroll_y;
    uint32_t page_count;
    uint32_t page_index;

    if(gallery == NULL || gallery->page_height <= 0) return 0u;
    page_count = photo_gallery_page_count_for(gallery);
    if(page_count == 0u) return 0u;
    scroll_y = photo_gallery_current_scroll_y(gallery);
    page_index = (uint32_t)((scroll_y + gallery->page_height / 2) /
                            gallery->page_height);
    return photo_gallery_clamp_page(gallery, page_index);
}

static void photo_gallery_apply_page_position(
    photo_gallery_t *gallery, uint32_t page_index, lv_anim_enable_t animation)
{
    int32_t target_y;

    if(gallery == NULL || gallery->building || gallery->list_view == NULL ||
       gallery->page_height <= 0) return;
    page_index = photo_gallery_clamp_page(gallery, page_index);
    target_y = (int32_t)page_index * gallery->page_height;
    if(target_y > gallery->scroll_max) target_y = gallery->scroll_max;
    lv_obj_scroll_to_y(gallery->list_view, target_y, animation);
}

static void photo_gallery_correct_to_current_page(photo_gallery_t *gallery)
{
    if(gallery == NULL || gallery->list_view == NULL) return;
    gallery->correcting_scroll = true;
    photo_gallery_apply_page_position(
        gallery, gallery->current_page, LV_ANIM_OFF);
    gallery->correcting_scroll = false;
}

static void photo_gallery_refresh_visible(photo_gallery_t *gallery)
{
    bool claimed[PHOTO_GALLERY_MAX_CACHE_ITEMS];
    uint32_t page_count;
    uint32_t items_per_page;
    uint32_t first_page;
    uint32_t last_page;
    uint32_t visible_from;
    uint32_t visible_to;
    uint32_t desired_from;
    uint32_t desired_to;
    uint32_t missing_from = PHOTO_GALLERY_INVALID_INDEX;
    uint32_t missing_to = 0u;
    uint32_t scroll_y;
    uint32_t list_height;
    uint32_t index;

    if(gallery == NULL || gallery->building || gallery->list_view == NULL ||
       gallery->cache[0].root == NULL ||
       gallery->item_count == 0u || gallery->cache_size == 0u) return;
    page_count = photo_gallery_page_count_for(gallery);
    items_per_page = photo_gallery_items_per_page(gallery);
    scroll_y = (uint32_t)photo_gallery_current_scroll_y(gallery);
    list_height = (uint32_t)lv_obj_get_height(gallery->list_view);
    if(gallery->page_height <= 0 || list_height == 0u) return;

    first_page = scroll_y / (uint32_t)gallery->page_height;
    last_page = (scroll_y + list_height - 1u) /
                (uint32_t)gallery->page_height;
    if(first_page >= page_count) first_page = page_count - 1u;
    if(last_page >= page_count) last_page = page_count - 1u;
    visible_from = first_page * items_per_page;
    visible_to = (last_page + 1u) * items_per_page - 1u;
    if(visible_to >= gallery->item_count) visible_to = gallery->item_count - 1u;
    desired_from = visible_from > gallery->prefetch_items
                       ? visible_from - gallery->prefetch_items
                       : 0u;
    desired_to = gallery->prefetch_items >
                         gallery->item_count - 1u - visible_to
                     ? gallery->item_count - 1u
                     : visible_to + gallery->prefetch_items;
    if(desired_to - desired_from + 1u > gallery->cache_size) {
        desired_from = visible_from;
        desired_to = visible_to;
        if(desired_to - desired_from + 1u > gallery->cache_size) {
            desired_to = desired_from + gallery->cache_size - 1u;
            if(desired_to >= gallery->item_count) {
                desired_to = gallery->item_count - 1u;
                desired_from = desired_to + 1u - gallery->cache_size;
            }
        }
    }

    memset(claimed, 0, sizeof(claimed));
    for(index = desired_from; index <= desired_to; ++index) {
        int32_t slot;
        uint32_t page_index = index / items_per_page;
        uint32_t item_in_page = index % items_per_page;
        uint32_t row = item_in_page / gallery->columns;
        uint32_t column = item_in_page % gallery->columns;
        bool visible = index >= visible_from && index <= visible_to;

        slot = photo_gallery_find_cache_slot(gallery, index);
        if(slot < 0) slot = photo_gallery_choose_cache_slot(gallery, claimed);
        if(slot < 0) continue;
        claimed[slot] = true;
        if(!photo_gallery_render_cache_item(
               gallery, &gallery->cache[slot], index, visible)) {
            if(missing_from == PHOTO_GALLERY_INVALID_INDEX) {
                missing_from = index;
            }
            missing_to = index;
        }
        lv_obj_set_pos(
            gallery->cache[slot].root,
            (int32_t)gallery->padding_left +
                (int32_t)column *
                    ((int32_t)gallery->thumbnail_width + gallery->gap_x),
            (int32_t)page_index * gallery->page_height +
                (int32_t)gallery->padding_top +
                (int32_t)row *
                    ((int32_t)gallery->thumbnail_height + gallery->gap_y));
    }
    for(uint8_t slot = 0u; slot < gallery->cache_size; ++slot) {
        if(!claimed[slot]) photo_gallery_clear_cache_item(&gallery->cache[slot]);
    }
    if(missing_from != PHOTO_GALLERY_INVALID_INDEX &&
       gallery->request_range != NULL &&
       (!gallery->request_valid || gallery->request_from != missing_from ||
        gallery->request_to != missing_to)) {
        gallery->request_valid = true;
        gallery->request_from = missing_from;
        gallery->request_to = missing_to;
        gallery->request_range(gallery->user_ctx, missing_from, missing_to);
    }
}

static void photo_gallery_refresh_layout(photo_gallery_t *gallery)
{
    int32_t list_width;
    int32_t list_height;
    int32_t content_width;
    int32_t grid_width;
    int32_t x_offset;
    uint32_t page_count;
    uint32_t content_height;

    if(gallery == NULL || gallery->list_view == NULL ||
       gallery->scroll_extent == NULL || gallery->layout_refreshing) return;
    gallery->layout_refreshing = true;
    lv_obj_update_layout(gallery->root);
    lv_obj_update_layout(gallery->list_view);
    list_width = lv_obj_get_width(gallery->list_view);
    list_height = lv_obj_get_height(gallery->list_view);
    if(list_width <= 0 || list_height <= 0) {
        gallery->layout_refreshing = false;
        return;
    }
    gallery->page_height = list_height;
    content_width = list_width - gallery->base_padding_left -
                    gallery->base_padding_right;
    grid_width = (int32_t)gallery->columns * gallery->thumbnail_width +
                 (int32_t)(gallery->columns - 1u) * gallery->gap_x;
    x_offset = gallery->base_padding_left;
    if(content_width > grid_width) x_offset += (content_width - grid_width) / 2;
    gallery->padding_left = (uint16_t)x_offset;
    page_count = photo_gallery_page_count_for(gallery);
    content_height = page_count == 0u
                         ? (uint32_t)list_height
                         : page_count * (uint32_t)list_height;
    lv_obj_set_width(gallery->scroll_extent, list_width);
    lv_obj_set_height(gallery->scroll_extent, 1);
    lv_obj_set_pos(
        gallery->scroll_extent,
        0,
        content_height > 0u ? (int32_t)content_height - 1 : 0);
    gallery->scroll_max = content_height > (uint32_t)list_height
                             ? (int32_t)(content_height - list_height)
                             : 0;
    gallery->geometry_valid = true;
    gallery->current_page = page_count == 0u
                                ? 0u
                                : photo_gallery_clamp_page(
                                      gallery, gallery->current_page);
    photo_gallery_update_page_label(gallery);
    photo_gallery_refresh_visible(gallery);
    gallery->layout_refreshing = false;
}

static void photo_gallery_on_root_delete(lv_event_t *event)
{
    photo_gallery_t *gallery = lv_event_get_user_data(event);

    if(gallery == NULL) return;
    lv_free(gallery->selected);
    gallery->selected = NULL;
    gallery->root = NULL;
    lv_free(gallery);
}

static void photo_gallery_on_item_clicked(lv_event_t *event)
{
    photo_gallery_cache_item_t *cache_item = lv_event_get_user_data(event);
    photo_gallery_t *gallery;
    photo_gallery_item_t item;
    bool selected;

    if(cache_item == NULL || cache_item->root == NULL ||
       cache_item->index == PHOTO_GALLERY_INVALID_INDEX) return;
    gallery = cache_item->gallery;
    if(gallery == NULL || !photo_gallery_get_item(gallery, cache_item->index, &item)) {
        return;
    }
    if(item.disabled) return;
    if(gallery->selection_mode) {
        selected = gallery->selected[cache_item->index] == 0u;
        gallery->selected[cache_item->index] = selected ? 1u : 0u;
        gallery->selected_count += selected ? 1u : (uint32_t)-1;
        photo_gallery_render_cache_item(gallery, cache_item, cache_item->index, true);
        photo_gallery_emit(
            gallery,
            PHOTO_GALLERY_EVENT_SELECTION_CHANGED,
            cache_item->index,
            item.id,
            gallery->current_page,
            LV_DIR_NONE,
            selected);
    }
    else {
        photo_gallery_emit(
            gallery,
            PHOTO_GALLERY_EVENT_ITEM_CLICKED,
            cache_item->index,
            item.id,
            gallery->current_page,
            LV_DIR_NONE,
            false);
    }
}

static void photo_gallery_on_scroll_begin(lv_event_t *event)
{
    photo_gallery_t *gallery = lv_event_get_user_data(event);
    lv_anim_t *animation = lv_event_get_scroll_anim(event);

    if(gallery == NULL || gallery->suppress_scroll_end ||
       gallery->correcting_scroll) return;
    if(animation == NULL) {
        gallery->gesture_start_page = photo_gallery_nearest_page(gallery);
        gallery->release_page_pending = false;
    }
    gallery->scroll_active = true;
    photo_gallery_refresh_visible(gallery);
}

static void photo_gallery_on_scroll(lv_event_t *event)
{
    photo_gallery_t *gallery = lv_event_get_user_data(event);

    if(gallery == NULL) return;
    photo_gallery_refresh_visible(gallery);
}

static uint32_t photo_gallery_release_page(
    photo_gallery_t *gallery, int32_t velocity_y)
{
    uint32_t page_count;
    int32_t start_y;
    int32_t scroll_y;
    int32_t distance;

    page_count = photo_gallery_page_count_for(gallery);
    if(page_count == 0u) return 0u;
    if(velocity_y < -PHOTO_GALLERY_FLICK_VELOCITY) {
        return gallery->gesture_start_page + 1u < page_count
                   ? gallery->gesture_start_page + 1u
                   : gallery->gesture_start_page;
    }
    if(velocity_y > PHOTO_GALLERY_FLICK_VELOCITY) {
        return gallery->gesture_start_page > 0u
                   ? gallery->gesture_start_page - 1u
                   : gallery->gesture_start_page;
    }
    start_y = (int32_t)gallery->gesture_start_page * gallery->page_height;
    scroll_y = photo_gallery_current_scroll_y(gallery);
    distance = scroll_y - start_y;
    if(distance > 0 && gallery->gesture_start_page + 1u < page_count &&
       (int64_t)distance * 1000 >=
           (int64_t)gallery->page_height *
               PHOTO_GALLERY_RELEASE_THRESHOLD_PER_MILLE) {
        return gallery->gesture_start_page + 1u;
    }
    if(distance < 0 && gallery->gesture_start_page > 0u &&
       (int64_t)(-distance) * 1000 >=
           (int64_t)gallery->page_height *
               PHOTO_GALLERY_RELEASE_THRESHOLD_PER_MILLE) {
        return gallery->gesture_start_page - 1u;
    }
    return gallery->gesture_start_page;
}

static void photo_gallery_on_scroll_throw_begin(lv_event_t *event)
{
    photo_gallery_t *gallery = lv_event_get_user_data(event);
    lv_indev_t *indev;
    lv_point_t vector;

    if(gallery == NULL || !gallery->snap_to_page ||
       gallery->programmatic_scroll || !gallery->scroll_active) return;
    indev = lv_event_get_param(event);
    if(indev == NULL) indev = lv_indev_active();
    if(indev == NULL) return;
    lv_indev_get_vect(indev, &vector);
    gallery->release_page = photo_gallery_release_page(gallery, vector.y);
    gallery->release_page_pending = true;
}

static void photo_gallery_commit_page(
    photo_gallery_t *gallery, uint32_t page_index, bool emit_event)
{
    bool changed;

    page_index = photo_gallery_clamp_page(gallery, page_index);
    changed = gallery->current_page != page_index;
    gallery->current_page = page_index;
    photo_gallery_update_page_label(gallery);
    if(emit_event && changed) {
        photo_gallery_emit(
            gallery,
            PHOTO_GALLERY_EVENT_PAGE_CHANGED,
            page_index * photo_gallery_items_per_page(gallery),
            0u,
            page_index,
            LV_DIR_NONE,
            false);
    }
}

static void photo_gallery_on_scroll_end(lv_event_t *event)
{
    photo_gallery_t *gallery = lv_event_get_user_data(event);
    lv_indev_t *indev = lv_indev_active();
    uint32_t target_page;

    if(gallery == NULL || gallery->suppress_scroll_end ||
       gallery->correcting_scroll) return;
    if(indev != NULL && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
        return;
    }
    if(gallery->snap_to_page) {
        if(gallery->programmatic_scroll) {
            target_page = gallery->target_page;
            gallery->programmatic_scroll = false;
        }
        else if(gallery->release_page_pending) {
            target_page = gallery->release_page;
            gallery->release_page_pending = false;
        }
        else {
            target_page = photo_gallery_release_page(gallery, 0);
        }
        if(photo_gallery_current_scroll_y(gallery) !=
           (int32_t)target_page * gallery->page_height) {
            gallery->target_page = target_page;
            gallery->programmatic_scroll = true;
            photo_gallery_apply_page_position(
                gallery, target_page, LV_ANIM_ON);
            return;
        }
        photo_gallery_commit_page(gallery, target_page, true);
    }
    else {
        photo_gallery_commit_page(
            gallery, photo_gallery_nearest_page(gallery), true);
    }
    gallery->scroll_active = false;
    gallery->programmatic_scroll = false;
    gallery->target_page = 0u;
    photo_gallery_refresh_visible(gallery);
}

static void photo_gallery_on_size_changed(lv_event_t *event)
{
    photo_gallery_t *gallery = lv_event_get_user_data(event);

    if(gallery == NULL || gallery->scroll_active ||
       gallery->layout_refreshing ||
       gallery->programmatic_scroll) return;
    gallery->geometry_valid = false;
    photo_gallery_refresh_layout(gallery);
    photo_gallery_correct_to_current_page(gallery);
}

static void photo_gallery_on_viewer_close(lv_event_t *event)
{
    photo_gallery_t *gallery = lv_event_get_user_data(event);

    photo_gallery_close_viewer(gallery, LV_ANIM_ON);
}

static void photo_gallery_on_viewer_previous(lv_event_t *event)
{
    photo_gallery_t *gallery = lv_event_get_user_data(event);

    if(gallery == NULL || gallery->viewer_index == 0u) {
        if(gallery != NULL) {
            photo_gallery_emit(
                gallery,
                PHOTO_GALLERY_EVENT_VIEWER_EDGE_REACHED,
                gallery->viewer_index,
                gallery->viewer_id,
                gallery->current_page,
                LV_DIR_RIGHT,
                false);
        }
        return;
    }
    photo_gallery_viewer_set_index(
        gallery, gallery->viewer_index - 1u, LV_ANIM_ON);
}

static void photo_gallery_on_viewer_next(lv_event_t *event)
{
    photo_gallery_t *gallery = lv_event_get_user_data(event);

    if(gallery == NULL || gallery->viewer_index + 1u >= gallery->item_count) {
        if(gallery != NULL) {
            photo_gallery_emit(
                gallery,
                PHOTO_GALLERY_EVENT_VIEWER_EDGE_REACHED,
                gallery->viewer_index,
                gallery->viewer_id,
                gallery->current_page,
                LV_DIR_LEFT,
                false);
        }
        return;
    }
    photo_gallery_viewer_set_index(
        gallery, gallery->viewer_index + 1u, LV_ANIM_ON);
}

static void photo_gallery_on_viewer_pressed(lv_event_t *event)
{
    photo_gallery_t *gallery = lv_event_get_user_data(event);
    lv_indev_t *indev;
    lv_point_t point;

    if(gallery == NULL) return;
    indev = lv_event_get_indev(event);
    if(indev == NULL) return;
    lv_indev_get_point(indev, &point);
    gallery->viewer_press_x = point.x;
    gallery->viewer_dragging = true;
}

static void photo_gallery_on_viewer_released(lv_event_t *event)
{
    photo_gallery_t *gallery = lv_event_get_user_data(event);
    lv_indev_t *indev;
    lv_point_t point;
    int32_t delta_x;

    if(gallery == NULL || !gallery->viewer_dragging) return;
    gallery->viewer_dragging = false;
    indev = lv_event_get_indev(event);
    if(indev == NULL) return;
    lv_indev_get_point(indev, &point);
    delta_x = point.x - gallery->viewer_press_x;
    if(delta_x <= -(int32_t)gallery->viewer_swipe_threshold) {
        photo_gallery_on_viewer_next(event);
    }
    else if(delta_x >= (int32_t)gallery->viewer_swipe_threshold) {
        photo_gallery_on_viewer_previous(event);
    }
}

static bool photo_gallery_copy_config(
    photo_gallery_t *gallery, const photo_gallery_config_t *config)
{
    uint32_t max_items_per_page;

    if(gallery == NULL || config == NULL ||
       (config->items == NULL && config->item_provider == NULL)) {
        return false;
    }
    gallery->items = config->items;
    gallery->item_provider = config->item_provider;
    gallery->request_range = config->request_range;
    gallery->on_event = config->on_event;
    gallery->user_ctx = config->user_ctx;
    gallery->columns = config->columns > 0u ? config->columns
                                            : PHOTO_GALLERY_DEFAULT_COLUMNS;
    gallery->rows_per_page = config->rows_per_page > 0u
                                 ? config->rows_per_page
                                 : PHOTO_GALLERY_DEFAULT_ROWS;
    max_items_per_page = PHOTO_GALLERY_MAX_CACHE_ITEMS / gallery->columns;
    if(max_items_per_page == 0u) {
        gallery->columns = PHOTO_GALLERY_MAX_CACHE_ITEMS;
        max_items_per_page = 1u;
    }
    if(gallery->rows_per_page > max_items_per_page) {
        gallery->rows_per_page = (uint8_t)max_items_per_page;
    }
    gallery->cache_size = config->cache_size > 0u
                              ? config->cache_size
                              : PHOTO_GALLERY_DEFAULT_CACHE;
    if(gallery->cache_size > PHOTO_GALLERY_MAX_CACHE_ITEMS) {
        gallery->cache_size = PHOTO_GALLERY_MAX_CACHE_ITEMS;
    }
    if(gallery->cache_size < gallery->columns * gallery->rows_per_page) {
        gallery->cache_size = gallery->columns * gallery->rows_per_page;
    }
    if(gallery->cache_size > PHOTO_GALLERY_MAX_CACHE_ITEMS) {
        gallery->cache_size = PHOTO_GALLERY_MAX_CACHE_ITEMS;
    }
    gallery->prefetch_items = config->prefetch_items > 0u
                                  ? config->prefetch_items
                                  : PHOTO_GALLERY_DEFAULT_PREFETCH;
    gallery->thumbnail_width = config->thumbnail_width > 0u
                                   ? config->thumbnail_width
                                   : PHOTO_GALLERY_DEFAULT_THUMB_WIDTH;
    gallery->thumbnail_height = config->thumbnail_height > 0u
                                    ? config->thumbnail_height
                                    : PHOTO_GALLERY_DEFAULT_THUMB_HEIGHT;
    gallery->gap_x = config->gap_x > 0u ? config->gap_x : PHOTO_GALLERY_DEFAULT_GAP_X;
    gallery->gap_y = config->gap_y > 0u ? config->gap_y : PHOTO_GALLERY_DEFAULT_GAP_Y;
    gallery->padding_left = config->padding_left > 0u
                                ? config->padding_left
                                : PHOTO_GALLERY_DEFAULT_PADDING_LEFT;
    gallery->padding_right = config->padding_right > 0u
                                 ? config->padding_right
                                 : PHOTO_GALLERY_DEFAULT_PADDING_RIGHT;
    gallery->base_padding_left = gallery->padding_left;
    gallery->base_padding_right = gallery->padding_right;
    gallery->padding_top = config->padding_top > 0u
                               ? config->padding_top
                               : PHOTO_GALLERY_DEFAULT_PADDING_TOP;
    gallery->padding_bottom = config->padding_bottom > 0u
                                  ? config->padding_bottom
                                  : PHOTO_GALLERY_DEFAULT_PADDING_BOTTOM;
    gallery->viewer_swipe_threshold = config->viewer_swipe_threshold > 0u
                                          ? config->viewer_swipe_threshold
                                          : PHOTO_GALLERY_DEFAULT_SWIPE_THRESHOLD;
    gallery->title = config->title;
    gallery->snap_to_page = config->snap_to_page;
    gallery->selection_mode = config->selection_mode;
    gallery->title_font = config->title_font;
    gallery->body_font = config->body_font;
    gallery->item_count = config->item_count;
    return true;
}

static bool photo_gallery_allocate_selection(
    photo_gallery_t *gallery, uint32_t item_count)
{
    uint8_t *old_selected;
    uint8_t *selected;
    uint32_t old_count;
    uint32_t copy_count;
    uint32_t selected_count = 0u;
    uint32_t index;

    old_selected = gallery->selected;
    old_count = gallery->item_count;
    if(item_count == 0u) {
        lv_free(gallery->selected);
        gallery->selected = NULL;
        gallery->selected_count = 0u;
        return true;
    }
    selected = lv_malloc_zeroed((size_t)item_count);
    if(selected == NULL) return false;
    copy_count = old_count < item_count
                     ? old_count
                     : item_count;
    if(old_selected != NULL && copy_count > 0u) {
        memcpy(selected, old_selected, copy_count);
    }
    for(index = 0u; index < copy_count; ++index) {
        selected_count += selected[index] != 0u ? 1u : 0u;
    }
    lv_free(old_selected);
    gallery->selected = selected;
    gallery->selected_count = selected_count;
    return true;
}

static bool photo_gallery_create_cache_item(
    photo_gallery_t *gallery, uint8_t slot)
{
    photo_gallery_cache_item_t *cache_item = &gallery->cache[slot];

    cache_item->root = lv_obj_create(gallery->list_view);
    if(cache_item->root == NULL) return false;
    cache_item->gallery = gallery;
    photo_gallery_make_plain(cache_item->root);
    lv_obj_set_size(
        cache_item->root,
        gallery->thumbnail_width,
        gallery->thumbnail_height);
    lv_obj_set_style_bg_color(cache_item->root, lv_color_hex(0x20262D), 0);
    lv_obj_set_style_bg_opa(cache_item->root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cache_item->root, 1, 0);
    lv_obj_set_style_border_color(cache_item->root, lv_color_hex(0x4A5561), 0);
    lv_obj_set_style_radius(cache_item->root, 10, 0);
    lv_obj_set_style_clip_corner(cache_item->root, true, 0);
    lv_obj_add_flag(cache_item->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        cache_item->root,
        photo_gallery_on_item_clicked,
        LV_EVENT_CLICKED,
        cache_item);

    cache_item->image = lv_image_create(cache_item->root);
    if(cache_item->image == NULL) return false;
    lv_obj_set_size(cache_item->image, LV_PCT(100), LV_PCT(100));
    lv_image_set_inner_align(cache_item->image, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_remove_flag(cache_item->image, LV_OBJ_FLAG_CLICKABLE);

    cache_item->title = lv_label_create(cache_item->root);
    if(cache_item->title == NULL) return false;
    lv_obj_set_width(cache_item->title, LV_PCT(100));
    lv_obj_align(cache_item->title, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_left(cache_item->title, 8, 0);
    lv_obj_set_style_pad_right(cache_item->title, 8, 0);
    lv_obj_set_style_pad_top(cache_item->title, 4, 0);
    lv_obj_set_style_pad_bottom(cache_item->title, 4, 0);
    lv_obj_set_style_bg_color(cache_item->title, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(cache_item->title, LV_OPA_70, 0);
    lv_obj_set_style_text_color(cache_item->title, lv_color_white(), 0);
    lv_obj_set_style_text_font(
        cache_item->title,
        photo_gallery_font_or_default(gallery->title_font),
        0);
    lv_label_set_long_mode(cache_item->title, LV_LABEL_LONG_DOT);
    lv_obj_remove_flag(cache_item->title, LV_OBJ_FLAG_CLICKABLE);

    cache_item->checked = lv_label_create(cache_item->root);
    if(cache_item->checked == NULL) return false;
    lv_label_set_text(cache_item->checked, LV_SYMBOL_OK);
    lv_obj_align(cache_item->checked, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_set_style_text_color(cache_item->checked, lv_color_hex(0x33D17A), 0);
    lv_obj_set_style_text_font(
        cache_item->checked,
        photo_gallery_font_or_default(gallery->body_font),
        0);
    lv_obj_add_flag(cache_item->checked, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(cache_item->checked, LV_OBJ_FLAG_CLICKABLE);
    photo_gallery_clear_cache_item(cache_item);
    return true;
}

static bool photo_gallery_build(photo_gallery_t *gallery, lv_obj_t *parent)
{
    uint8_t slot;

    gallery->building = true;
    gallery->root = lv_obj_create(parent);
    if(gallery->root == NULL) return false;
    photo_gallery_make_plain(gallery->root);
    lv_obj_set_size(gallery->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(gallery->root, lv_color_hex(0x0D1117), 0);
    lv_obj_set_style_bg_opa(gallery->root, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(
        gallery->root,
        photo_gallery_on_root_delete,
        LV_EVENT_DELETE,
        gallery);

    gallery->list_view = lv_obj_create(gallery->root);
    if(gallery->list_view == NULL) return false;
    photo_gallery_make_plain(gallery->list_view);
    lv_obj_set_size(gallery->list_view, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(gallery->list_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(gallery->list_view, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_dir(gallery->list_view, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(gallery->list_view, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(gallery->list_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(gallery->list_view, 0, 0);
    lv_obj_add_event_cb(
        gallery->list_view,
        photo_gallery_on_scroll_begin,
        LV_EVENT_SCROLL_BEGIN,
        gallery);
    lv_obj_add_event_cb(
        gallery->list_view,
        photo_gallery_on_scroll,
        LV_EVENT_SCROLL,
        gallery);
    lv_obj_add_event_cb(
        gallery->list_view,
        photo_gallery_on_scroll_throw_begin,
        LV_EVENT_SCROLL_THROW_BEGIN,
        gallery);
    lv_obj_add_event_cb(
        gallery->list_view,
        photo_gallery_on_scroll_end,
        LV_EVENT_SCROLL_END,
        gallery);
    lv_obj_add_event_cb(
        gallery->list_view,
        photo_gallery_on_size_changed,
        LV_EVENT_SIZE_CHANGED,
        gallery);
    lv_obj_add_event_cb(
        gallery->list_view,
        photo_gallery_on_size_changed,
        LV_EVENT_LAYOUT_CHANGED,
        gallery);

    gallery->scroll_extent = lv_obj_create(gallery->list_view);
    if(gallery->scroll_extent == NULL) return false;
    photo_gallery_make_plain(gallery->scroll_extent);
    lv_obj_set_style_bg_opa(gallery->scroll_extent, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(
        gallery->scroll_extent,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    gallery->empty_label = lv_label_create(gallery->root);
    if(gallery->empty_label == NULL) return false;
    lv_label_set_text(gallery->empty_label, "No photos");
    lv_obj_set_style_text_color(gallery->empty_label, lv_color_hex(0xB8C1CC), 0);
    lv_obj_set_style_text_font(
        gallery->empty_label,
        photo_gallery_font_or_default(gallery->body_font),
        0);
    lv_obj_align(gallery->empty_label, LV_ALIGN_CENTER, 0, 0);

    gallery->page_label = lv_label_create(gallery->root);
    if(gallery->page_label == NULL) return false;
    lv_obj_set_style_text_color(gallery->page_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(
        gallery->page_label,
        photo_gallery_font_or_default(gallery->body_font),
        0);
    lv_obj_set_style_bg_color(gallery->page_label, lv_color_hex(0x20262D), 0);
    lv_obj_set_style_bg_opa(gallery->page_label, LV_OPA_80, 0);
    lv_obj_set_style_radius(gallery->page_label, 14, 0);
    lv_obj_set_style_pad_left(gallery->page_label, 12, 0);
    lv_obj_set_style_pad_right(gallery->page_label, 12, 0);
    lv_obj_set_style_pad_top(gallery->page_label, 5, 0);
    lv_obj_set_style_pad_bottom(gallery->page_label, 5, 0);
    lv_obj_align(gallery->page_label, LV_ALIGN_BOTTOM_MID, 0, -12);

    gallery->title_label = lv_label_create(gallery->root);
    if(gallery->title_label == NULL) return false;
    lv_label_set_text(
        gallery->title_label,
        gallery->title != NULL ? gallery->title : "Photos");
    lv_obj_set_style_text_color(gallery->title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(
        gallery->title_label,
        photo_gallery_font_or_default(gallery->title_font),
        0);
    lv_obj_align(gallery->title_label, LV_ALIGN_TOP_LEFT, 20, 18);
    lv_obj_remove_flag(gallery->title_label, LV_OBJ_FLAG_CLICKABLE);

    for(slot = 0u; slot < gallery->cache_size; ++slot) {
        if(!photo_gallery_create_cache_item(gallery, slot)) return false;
    }

    gallery->viewer_root = lv_obj_create(gallery->root);
    if(gallery->viewer_root == NULL) return false;
    photo_gallery_make_plain(gallery->viewer_root);
    lv_obj_set_size(gallery->viewer_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(gallery->viewer_root, lv_color_hex(0x080A0D), 0);
    lv_obj_set_style_bg_opa(gallery->viewer_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(gallery->viewer_root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(
        gallery->viewer_root,
        photo_gallery_on_viewer_pressed,
        LV_EVENT_PRESSED,
        gallery);
    lv_obj_add_event_cb(
        gallery->viewer_root,
        photo_gallery_on_viewer_released,
        LV_EVENT_RELEASED,
        gallery);
    lv_obj_add_event_cb(
        gallery->viewer_root,
        photo_gallery_on_viewer_released,
        LV_EVENT_PRESS_LOST,
        gallery);

    gallery->viewer_image = lv_image_create(gallery->viewer_root);
    if(gallery->viewer_image == NULL) return false;
    lv_obj_set_size(gallery->viewer_image, LV_PCT(86), LV_PCT(72));
    lv_obj_align(gallery->viewer_image, LV_ALIGN_CENTER, 0, 4);
    lv_image_set_inner_align(gallery->viewer_image, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_remove_flag(gallery->viewer_image, LV_OBJ_FLAG_CLICKABLE);

    gallery->viewer_title = lv_label_create(gallery->viewer_root);
    if(gallery->viewer_title == NULL) return false;
    lv_obj_set_width(gallery->viewer_title, LV_PCT(70));
    lv_obj_set_style_text_color(gallery->viewer_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(
        gallery->viewer_title,
        photo_gallery_font_or_default(gallery->title_font),
        0);
    lv_obj_set_style_text_align(gallery->viewer_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(gallery->viewer_title, LV_LABEL_LONG_DOT);
    lv_obj_align(gallery->viewer_title, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_remove_flag(gallery->viewer_title, LV_OBJ_FLAG_CLICKABLE);

    gallery->viewer_index_label = lv_label_create(gallery->viewer_root);
    if(gallery->viewer_index_label == NULL) return false;
    lv_obj_set_style_text_color(
        gallery->viewer_index_label, lv_color_hex(0xB8C1CC), 0);
    lv_obj_set_style_text_font(
        gallery->viewer_index_label,
        photo_gallery_font_or_default(gallery->body_font),
        0);
    lv_obj_align(gallery->viewer_index_label, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_remove_flag(gallery->viewer_index_label, LV_OBJ_FLAG_CLICKABLE);

    gallery->viewer_close = lv_button_create(gallery->viewer_root);
    gallery->viewer_previous = lv_button_create(gallery->viewer_root);
    gallery->viewer_next = lv_button_create(gallery->viewer_root);
    if(gallery->viewer_close == NULL || gallery->viewer_previous == NULL ||
       gallery->viewer_next == NULL) return false;
    lv_obj_set_size(gallery->viewer_close, 58, 46);
    lv_obj_align(gallery->viewer_close, LV_ALIGN_TOP_LEFT, 14, 12);
    lv_obj_set_size(gallery->viewer_previous, 58, 64);
    lv_obj_align(gallery->viewer_previous, LV_ALIGN_LEFT_MID, 14, 16);
    lv_obj_set_size(gallery->viewer_next, 58, 64);
    lv_obj_align(gallery->viewer_next, LV_ALIGN_RIGHT_MID, -14, 16);
    lv_obj_set_style_radius(gallery->viewer_close, 10, 0);
    lv_obj_set_style_radius(gallery->viewer_previous, 10, 0);
    lv_obj_set_style_radius(gallery->viewer_next, 10, 0);
    lv_obj_set_style_bg_color(gallery->viewer_close, lv_color_hex(0x252B33), 0);
    lv_obj_set_style_bg_color(gallery->viewer_previous, lv_color_hex(0x252B33), 0);
    lv_obj_set_style_bg_color(gallery->viewer_next, lv_color_hex(0x252B33), 0);
    lv_obj_set_style_bg_opa(gallery->viewer_close, LV_OPA_80, 0);
    lv_obj_set_style_bg_opa(gallery->viewer_previous, LV_OPA_80, 0);
    lv_obj_set_style_bg_opa(gallery->viewer_next, LV_OPA_80, 0);
    lv_obj_add_event_cb(
        gallery->viewer_close,
        photo_gallery_on_viewer_close,
        LV_EVENT_CLICKED,
        gallery);
    lv_obj_add_event_cb(
        gallery->viewer_previous,
        photo_gallery_on_viewer_previous,
        LV_EVENT_CLICKED,
        gallery);
    lv_obj_add_event_cb(
        gallery->viewer_next,
        photo_gallery_on_viewer_next,
        LV_EVENT_CLICKED,
        gallery);

    {
        lv_obj_t *close_label = lv_label_create(gallery->viewer_close);
        lv_obj_t *previous_label = lv_label_create(gallery->viewer_previous);
        lv_obj_t *next_label = lv_label_create(gallery->viewer_next);
        if(close_label == NULL || previous_label == NULL || next_label == NULL) {
            return false;
        }
        lv_label_set_text(close_label, LV_SYMBOL_LEFT);
        lv_label_set_text(previous_label, LV_SYMBOL_LEFT);
        lv_label_set_text(next_label, LV_SYMBOL_RIGHT);
        lv_obj_center(close_label);
        lv_obj_center(previous_label);
        lv_obj_center(next_label);
        lv_obj_set_style_text_font(
            close_label,
            photo_gallery_font_or_default(gallery->body_font),
            0);
        lv_obj_set_style_text_font(
            previous_label,
            photo_gallery_font_or_default(gallery->body_font),
            0);
        lv_obj_set_style_text_font(
            next_label,
            photo_gallery_font_or_default(gallery->body_font),
            0);
    }
    gallery->viewer_index = PHOTO_GALLERY_INVALID_INDEX;
    gallery->building = false;
    return true;
}

photo_gallery_t *photo_gallery_create(
    lv_obj_t *parent, const photo_gallery_config_t *config)
{
    photo_gallery_t *gallery;

    if(parent == NULL || config == NULL) return NULL;
    gallery = lv_malloc_zeroed(sizeof(*gallery));
    if(gallery == NULL) return NULL;
    for(uint8_t slot = 0u; slot < PHOTO_GALLERY_MAX_CACHE_ITEMS; ++slot) {
        gallery->cache[slot].index = PHOTO_GALLERY_INVALID_INDEX;
    }
    if(!photo_gallery_copy_config(gallery, config) ||
       !photo_gallery_allocate_selection(gallery, gallery->item_count) ||
       !photo_gallery_build(gallery, parent)) {
        if(gallery->root != NULL) lv_obj_delete(gallery->root);
        else lv_free(gallery->selected), lv_free(gallery);
        return NULL;
    }
    photo_gallery_refresh_layout(gallery);
    if(gallery->item_count == 0u) {
        lv_obj_remove_flag(gallery->empty_label, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(gallery->empty_label, LV_OBJ_FLAG_HIDDEN);
    }
    photo_gallery_update_page_label(gallery);
    return gallery;
}

void photo_gallery_destroy(photo_gallery_t *gallery)
{
    if(gallery == NULL) return;
    if(gallery->root != NULL) {
        lv_obj_delete(gallery->root);
    }
}

lv_obj_t *photo_gallery_root(photo_gallery_t *gallery)
{
    return gallery != NULL ? gallery->root : NULL;
}

lv_obj_t *photo_gallery_list(photo_gallery_t *gallery)
{
    return gallery != NULL ? gallery->list_view : NULL;
}

lv_obj_t *photo_gallery_viewer_root(photo_gallery_t *gallery)
{
    return gallery != NULL ? gallery->viewer_root : NULL;
}

void photo_gallery_set_item_count(
    photo_gallery_t *gallery, uint32_t item_count)
{
    if(gallery == NULL) return;
    if(gallery->item_count == item_count) {
        gallery->request_valid = false;
        photo_gallery_refresh_layout(gallery);
        return;
    }
    if(!photo_gallery_allocate_selection(gallery, item_count)) return;
    gallery->item_count = item_count;
    gallery->current_page = photo_gallery_clamp_page(gallery, gallery->current_page);
    gallery->request_valid = false;
    if(gallery->empty_label != NULL) {
        if(item_count == 0u) lv_obj_remove_flag(gallery->empty_label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(gallery->empty_label, LV_OBJ_FLAG_HIDDEN);
    }
    photo_gallery_refresh_layout(gallery);
    photo_gallery_correct_to_current_page(gallery);
    if(gallery->viewer_open && gallery->viewer_index >= item_count) {
        if(item_count == 0u) photo_gallery_close_viewer(gallery, LV_ANIM_OFF);
        else photo_gallery_set_viewer_index_internal(gallery, item_count - 1u, false);
    }
}

void photo_gallery_data_changed(
    photo_gallery_t *gallery, uint32_t from, uint32_t to)
{
    if(gallery == NULL || gallery->item_count == 0u) return;
    if(from >= gallery->item_count) return;
    if(to >= gallery->item_count) to = gallery->item_count - 1u;
    if(from > to) return;
    gallery->request_valid = false;
    photo_gallery_refresh_visible(gallery);
    if(gallery->viewer_open && gallery->viewer_index >= from &&
       gallery->viewer_index <= to) {
        photo_gallery_set_viewer_index_internal(
            gallery, gallery->viewer_index, false);
    }
}

uint32_t photo_gallery_item_count(const photo_gallery_t *gallery)
{
    return gallery != NULL ? gallery->item_count : 0u;
}

void photo_gallery_set_page(
    photo_gallery_t *gallery, uint32_t page_index, lv_anim_enable_t animation)
{
    if(gallery == NULL || gallery->page_height <= 0 ||
       photo_gallery_page_count_for(gallery) == 0u) return;
    page_index = photo_gallery_clamp_page(gallery, page_index);
    gallery->target_page = page_index;
    gallery->programmatic_scroll = true;
    photo_gallery_apply_page_position(gallery, page_index, animation);
    if(!animation || !lv_obj_is_scrolling(gallery->list_view)) {
        gallery->programmatic_scroll = false;
        photo_gallery_commit_page(gallery, page_index, true);
        photo_gallery_refresh_visible(gallery);
    }
}

uint32_t photo_gallery_current_page(const photo_gallery_t *gallery)
{
    return gallery != NULL ? gallery->current_page : 0u;
}

uint32_t photo_gallery_page_count(const photo_gallery_t *gallery)
{
    return photo_gallery_page_count_for(gallery);
}

void photo_gallery_refresh(photo_gallery_t *gallery)
{
    if(gallery == NULL) return;
    gallery->geometry_valid = false;
    photo_gallery_refresh_layout(gallery);
    photo_gallery_correct_to_current_page(gallery);
}

void photo_gallery_set_selection_mode(
    photo_gallery_t *gallery, bool enabled)
{
    if(gallery == NULL) return;
    gallery->selection_mode = enabled;
    photo_gallery_refresh_visible(gallery);
}

bool photo_gallery_selection_mode(const photo_gallery_t *gallery)
{
    return gallery != NULL && gallery->selection_mode;
}

void photo_gallery_set_item_selected(
    photo_gallery_t *gallery, uint32_t index, bool selected)
{
    bool current;

    if(gallery == NULL || gallery->selected == NULL ||
       index >= gallery->item_count) return;
    current = gallery->selected[index] != 0u;
    if(current == selected) return;
    gallery->selected[index] = selected ? 1u : 0u;
    gallery->selected_count += selected ? 1u : (uint32_t)-1;
    photo_gallery_refresh_visible(gallery);
}

bool photo_gallery_item_selected(
    const photo_gallery_t *gallery, uint32_t index)
{
    if(gallery == NULL || gallery->selected == NULL ||
       index >= gallery->item_count) return false;
    return gallery->selected[index] != 0u;
}

uint32_t photo_gallery_selected_count(const photo_gallery_t *gallery)
{
    return gallery != NULL ? gallery->selected_count : 0u;
}

void photo_gallery_clear_selection(photo_gallery_t *gallery)
{
    if(gallery == NULL || gallery->selected == NULL) return;
    memset(gallery->selected, 0, gallery->item_count);
    gallery->selected_count = 0u;
    photo_gallery_refresh_visible(gallery);
}

static bool photo_gallery_set_viewer_index_internal(
    photo_gallery_t *gallery, uint32_t index, bool emit_event)
{
    photo_gallery_item_t item;
    bool available;
    const void *image_src;
    uint32_t old_index;

    if(gallery == NULL || !gallery->viewer_open ||
       index >= gallery->item_count) return false;
    old_index = gallery->viewer_index;
    gallery->viewer_index = index;
    available = photo_gallery_get_item(gallery, index, &item);
    gallery->viewer_id = available ? item.id : 0u;
    image_src = available
                    ? (item.image_src != NULL
                           ? item.image_src
                           : item.thumbnail_src)
                    : NULL;
    lv_image_set_src(gallery->viewer_image, image_src);
    lv_label_set_text(
        gallery->viewer_title,
        available && item.title != NULL ? item.title :
        (available ? "" : "Loading"));
    lv_label_set_text_fmt(
        gallery->viewer_index_label,
        "%u / %u",
        (unsigned)(index + 1u),
        (unsigned)gallery->item_count);
    if(!available && gallery->request_range != NULL &&
       (!gallery->request_valid || gallery->request_from != index ||
        gallery->request_to != index)) {
        gallery->request_valid = true;
        gallery->request_from = index;
        gallery->request_to = index;
        gallery->request_range(gallery->user_ctx, index, index);
    }
    if(emit_event && old_index != index) {
        photo_gallery_emit(
            gallery,
            PHOTO_GALLERY_EVENT_VIEWER_INDEX_CHANGED,
            index,
            available ? item.id : 0u,
            index / photo_gallery_items_per_page(gallery),
            LV_DIR_NONE,
            false);
    }
    return true;
}

bool photo_gallery_show_viewer(
    photo_gallery_t *gallery,
    uint32_t index,
    lv_anim_enable_t animation)
{
    if(gallery == NULL || index >= gallery->item_count ||
       gallery->viewer_root == NULL) return false;
    (void)animation;
    gallery->viewer_open = true;
    gallery->viewer_dragging = false;
    lv_obj_remove_flag(gallery->viewer_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(gallery->viewer_root);
    if(!photo_gallery_set_viewer_index_internal(gallery, index, false)) {
        return false;
    }
    photo_gallery_emit(
        gallery,
        PHOTO_GALLERY_EVENT_VIEWER_OPENED,
        index,
        gallery->viewer_id,
        index / photo_gallery_items_per_page(gallery),
        LV_DIR_NONE,
        false);
    return true;
}

void photo_gallery_close_viewer(
    photo_gallery_t *gallery, lv_anim_enable_t animation)
{
    if(gallery == NULL || !gallery->viewer_open) return;
    (void)animation;
    gallery->viewer_open = false;
    gallery->viewer_dragging = false;
    lv_obj_add_flag(gallery->viewer_root, LV_OBJ_FLAG_HIDDEN);
    photo_gallery_emit(
        gallery,
        PHOTO_GALLERY_EVENT_VIEWER_CLOSED,
        gallery->viewer_index,
        gallery->viewer_id,
        gallery->current_page,
        LV_DIR_NONE,
        false);
}

bool photo_gallery_viewer_is_open(const photo_gallery_t *gallery)
{
    return gallery != NULL && gallery->viewer_open;
}

uint32_t photo_gallery_viewer_index(const photo_gallery_t *gallery)
{
    return gallery != NULL ? gallery->viewer_index : PHOTO_GALLERY_INVALID_INDEX;
}

bool photo_gallery_viewer_set_index(
    photo_gallery_t *gallery,
    uint32_t index,
    lv_anim_enable_t animation)
{
    bool result;

    if(gallery == NULL || !gallery->viewer_open) return false;
    (void)animation;
    result = photo_gallery_set_viewer_index_internal(gallery, index, false);
    if(result) {
        photo_gallery_emit(
            gallery,
            PHOTO_GALLERY_EVENT_VIEWER_INDEX_CHANGED,
            index,
            gallery->viewer_id,
            index / photo_gallery_items_per_page(gallery),
            LV_DIR_NONE,
            false);
    }
    return result;
}
