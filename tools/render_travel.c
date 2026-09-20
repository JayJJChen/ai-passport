/* Software-only 240x320 verification with the pinned LVGL and real UI code. */
#include "travel_ui.h"
#include "bsp_display_rounding.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LV_FONT_DECLARE(travel_ui_font_24);
static uint16_t frame[240 * 320], buffer[240 * 320];
static void check_labels(lv_obj_t *object, bool hidden) {
    hidden |= lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN);
    if (!hidden && lv_obj_check_type(object, &lv_label_class)) {
        const char *text = lv_label_get_text(object);
        if (*text) {
            const lv_font_t *font = lv_obj_get_style_text_font(object, LV_PART_MAIN);
            /* Parent confirmations deliberately include blank separator lines. */
            char normalized[1024]; unsigned length = 0;
            for (unsigned i = 0; text[i]; ++i) {
                if (text[i] == '\n' && (!i || text[i - 1] == '\n')) continue;
                assert(length + 1 < sizeof(normalized)); normalized[length++] = text[i];
            }
            normalized[length] = 0;
            assert(travel_text_check(normalized, font, 200, 20));
            lv_area_t box, parent;
            lv_obj_get_coords(object, &box); lv_obj_get_coords(lv_obj_get_parent(object), &parent);
            assert(box.x1 >= parent.x1 && box.y1 >= parent.y1 && box.x2 <= parent.x2 && box.y2 <= parent.y2);
            assert(box.x1 >= 0 && box.y1 >= 0 && box.x2 < 240 && box.y2 < 320);
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i) check_labels(lv_obj_get_child(object, i), hidden);
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    const uint16_t *source = (const uint16_t *)pixels;
    for (int y = area->y1; y <= area->y2; ++y) for (int x = area->x1; x <= area->x2; ++x) {
        uint16_t pixel = *source++;
        frame[y * 240 + x] = bsp_display_pixel_outside_rounded_rect(x, y, 240, 320, 30) ? 0 : pixel;
    }
    lv_display_flush_ready(display);
}
static void save_frame(const char *directory, const char *name) {
    lv_refr_now(NULL);
    check_labels(lv_screen_active(), false);
    char filename[1024]; snprintf(filename, sizeof(filename), "%s/%s.ppm", directory, name);
    FILE *fp = fopen(filename, "wb"); assert(fp);
    fprintf(fp, "P6\n240 320\n255\n");
    for (unsigned i = 0; i < 240 * 320; ++i) {
        unsigned r = (frame[i] >> 11) & 31, g = (frame[i] >> 5) & 63, b = frame[i] & 31;
        uint8_t rgb[3] = {(uint8_t)((r << 3) | (r >> 2)), (uint8_t)((g << 2) | (g >> 4)), (uint8_t)((b << 3) | (b >> 2))};
        fwrite(rgb, 1, 3, fp);
    }
    fclose(fp);
}
static void advance_animation(uint32_t milliseconds) {
    lv_tick_inc(milliseconds);
    lv_timer_handler();
}
int main(int argc, char **argv) {
    assert(argc == 3);
    FILE *fp = fopen(argv[1], "rb"); assert(fp); fseek(fp, 0, SEEK_END);
    size_t size = (size_t)ftell(fp); rewind(fp); uint8_t *data = malloc(size); assert(data);
    assert(fread(data, 1, size, fp) == size); fclose(fp);
    lv_init();
    lv_display_t *display = lv_display_create(240, 320);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    travel_content_t content;
    assert(travel_content_open(&content, data, size));
    assert(!travel_text_check("龘", content.body_font, 5, 2));
    assert(!travel_text_check("\xc0\xaf", content.body_font, 5, 2));
    assert(!travel_text_check("好好好好好好", content.body_font, 5, 2));
    assert(travel_text_check("招呼探索回应发现上一下一确定返回家长设置连接配网忘记网络目的地取消清除等待手机蓝牙成功失败未联网已保存请打开微信小程序重试密码错误超时离线仍可使用确认吗内容包不可用电量未知选择就绪进行中新？", &travel_ui_font_24, 200, 1));
    travel_model_t model; travel_model_init(&model); travel_ui_create(&content);
    travel_net_status_t net = {.state = TRAVEL_NET_OFFLINE};
    travel_stamp_record_t stamps = {.mask = 1, .timestamps = {1720000000, 0, 0, 0}};
    travel_custom_schedule_t custom = {0};
    travel_ui_refresh(&content, &model, net, 82, &stamps, &custom); save_frame(argv[2], "home");
    travel_ui_play_motion(TRAVEL_MOTION_WAVE); advance_animation(750); save_frame(argv[2], "motion-wave");
    travel_ui_play_motion(TRAVEL_MOTION_NOD); advance_animation(500); save_frame(argv[2], "motion-nod");
    travel_ui_play_motion(TRAVEL_MOTION_POINT_RIGHT); advance_animation(500); save_frame(argv[2], "motion-point-right");
    travel_ui_play_motion(TRAVEL_MOTION_POINT_LEFT); advance_animation(500); save_frame(argv[2], "motion-point-left");
    travel_ui_play_motion(TRAVEL_MOTION_WALK); advance_animation(600); save_frame(argv[2], "motion-walk");
    const travel_place_t *place = &content.places[0];
    for (size_t i = 0; i < place->task_count; ++i) {
        model.page = TRAVEL_TASK; model.task = i;
        travel_ui_refresh(&content, &model, net, 82, &stamps, &custom);
        char name[40]; snprintf(name, sizeof(name), "task%u", (unsigned)i); save_frame(argv[2], name);
    }
    model.page = TRAVEL_REPLY; travel_ui_refresh(&content, &model, net, 82, &stamps, &custom); save_frame(argv[2], "reply");
    model.page = TRAVEL_PASSPORT; travel_ui_refresh(&content, &model, net, -1, &stamps, &custom); save_frame(argv[2], "passport");
    model.page = TRAVEL_MAINTENANCE; net.state = TRAVEL_NET_CONNECTING;
    travel_ui_refresh(&content, &model, net, 82, &stamps, &custom); save_frame(argv[2], "maintenance");
    travel_ui_notify_trip_changed();
    model.page = TRAVEL_HOME; travel_ui_refresh(&content, &model, net, 82, &stamps, &custom);
    save_frame(argv[2], "synced-trip-walk");
    model.page = TRAVEL_STAMP_ANIM; travel_ui_refresh(&content, &model, net, 82, &stamps, &custom); save_frame(argv[2], "stamp_anim");
    model.page = TRAVEL_HOME; travel_ui_refresh(&content, &model, net, 82, &stamps, &custom);
    save_frame(argv[2], "stamp-success-nod");
    travel_ui_show_sync_success("新行程已就绪");
    for (size_t i = 1; i < content.place_count; ++i) {
        model.place = i; model.page = TRAVEL_HOME;
        travel_ui_refresh(&content, &model, net, 82, &stamps, &custom);
        char name[40]; snprintf(name, sizeof(name), "destination%u", (unsigned)i); save_frame(argv[2], name);
    }
    printf("Real LVGL font coverage and 240x320 software rendering: PASS, body line=%u, title line=%u\n",
        content.body_font->line_height, content.title_font->line_height);
    lv_obj_delete(lv_screen_active()); travel_content_close(&content); free(data);
    return 0;
}
