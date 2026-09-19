#include "travel_ui.h"
#include <stdio.h>

LV_FONT_DECLARE(travel_ui_font_24);
LV_FONT_DECLARE(travel_ui_font_36);
LV_FONT_DECLARE(lv_font_montserrat_14);
extern const lv_image_dsc_t koala_sprite;
static lv_obj_t *s_screen, *s_background, *s_koala, *s_title, *s_dialog, *s_dialog_text;
static lv_obj_t *s_parent, *s_parent_text, *s_status, *s_hints[3], *s_battery, *s_battery_fill, *s_network;
static lv_obj_t *s_stamp_box, *s_stamp_inner, *s_stamp_header, *s_stamp_title, *s_stamp_footer;
static lv_obj_t *s_stamp_info, *s_stamp_time, *s_stamp_page;
static lv_obj_t *s_time;
static const uint32_t INK = 0x123565, CREAM = 0xfff6df;

static void visible(lv_obj_t *obj, bool show) {
    if (show) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, int radius) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj, lv_color_hex(CREAM), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    return obj;
}
static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, int width) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(INK), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
    return obj;
}
void travel_ui_create(const travel_content_t *content) {
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(CREAM), 0);
    s_background = lv_image_create(s_screen);
    lv_obj_set_pos(s_background, 0, 0);
    s_koala = lv_image_create(s_screen);
    lv_image_set_src(s_koala, &koala_sprite); lv_obj_set_pos(s_koala, 20, 84);
    s_title = label(s_screen, content->title_font, 85); lv_obj_set_pos(s_title, 10, 10);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_LEFT, 0);
    s_time = lv_label_create(s_screen);
    lv_obj_set_width(s_time, 92);
    lv_obj_set_pos(s_time, 96, 9);
    lv_obj_set_style_text_font(s_time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_time, lv_color_hex(INK), 0);
    lv_obj_set_style_text_align(s_time, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_time, "09/19 10:00");
    s_dialog = panel(s_screen, 8, 230, 172, 82, 20);
    s_dialog_text = label(s_dialog, content->body_font, 156);
    lv_obj_set_style_text_line_space(s_dialog_text, 36 - content->body_font->line_height, 0);
    s_parent = panel(s_screen, 8, 80, 172, 232, 16);
    s_parent_text = label(s_parent, &travel_ui_font_24, 160);
    lv_obj_set_style_text_line_space(s_parent_text, 32 - travel_ui_font_24.line_height, 0);
    lv_obj_set_pos(s_parent_text, 6, 4);
    s_status = label(s_parent, &travel_ui_font_24, 160); lv_obj_set_pos(s_status, 6, 179);
    s_stamp_box = panel(s_parent, 24, 12, 124, 124, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(s_stamp_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_stamp_box, 3, 0);
    lv_obj_set_style_border_color(s_stamp_box, lv_color_hex(0xC93B2B), 0);
    s_stamp_inner = panel(s_stamp_box, 13, 13, 98, 98, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(s_stamp_inner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_stamp_inner, 1, 0);
    lv_obj_set_style_border_color(s_stamp_inner, lv_color_hex(0xC93B2B), 0);
    s_stamp_header = label(s_stamp_box, &lv_font_montserrat_14, 98);
    lv_obj_set_pos(s_stamp_header, 13, 14);
    lv_label_set_text(s_stamp_header, "PASSPORT");
    s_stamp_title = label(s_stamp_box, &travel_ui_font_24, 98);
    lv_obj_center(s_stamp_title);
    lv_label_set_text(s_stamp_title, "已确认");
    s_stamp_footer = label(s_stamp_box, &lv_font_montserrat_14, 98);
    lv_obj_set_pos(s_stamp_footer, 13, 84);
    lv_label_set_text(s_stamp_footer, "★ NO. 01 ★");
    s_stamp_info = label(s_parent, &travel_ui_font_24, 160);
    lv_obj_set_pos(s_stamp_info, 6, 144);
    s_stamp_time = label(s_parent, &lv_font_montserrat_14, 160);
    lv_obj_set_pos(s_stamp_time, 6, 178);
    s_stamp_page = label(s_parent, &lv_font_montserrat_14, 160);
    lv_obj_set_pos(s_stamp_page, 6, 202);
    for (unsigned i = 0; i < 3; ++i) {
        lv_obj_t *hint = panel(s_screen, 188, travel_key_center(i, 320) - 22, 52, 44, 12);
        s_hints[i] = label(hint, &travel_ui_font_24, 52);
        lv_obj_center(s_hints[i]);
    }
    s_battery = panel(s_screen, 194, 10, TRAVEL_BATTERY_GAUGE_W, TRAVEL_BATTERY_GAUGE_H, 2);
    lv_obj_set_style_border_color(s_battery, lv_color_hex(INK), 0);
    lv_obj_set_style_border_width(s_battery, TRAVEL_BATTERY_BORDER_W, 0);
    /* Child coordinates are relative to the parent content area (inside the 2 px border).
     * Placing the fill at (INNER_GAP, INNER_GAP) yields a symmetrical 1 px gap all around:
     * 2 px border + 1 px gap + 23 px max fill + 1 px gap + 2 px border = 29 px width,
     * 2 px border + 1 px gap + 6 px fill + 1 px gap + 2 px border = 12 px height. */
    s_battery_fill = panel(s_battery, TRAVEL_BATTERY_INNER_GAP, TRAVEL_BATTERY_INNER_GAP,
                           TRAVEL_BATTERY_FILL_MAX_W, TRAVEL_BATTERY_FILL_H, 0);
    s_network = panel(s_screen, 186, 12, 6, 6, 3);
    lv_screen_load(s_screen);
}
static const char *network_text(travel_net_state_t state) {
    switch (state) {
    case TRAVEL_NET_CONNECTED: return "已连接";
    case TRAVEL_NET_FAILED: return "连接失败";
    case TRAVEL_NET_TIMEOUT: return "连接超时";
    default: return "等待连接...";
    }
}
void travel_ui_set_time(const char *time_str) {
    if (s_time && time_str) {
        lv_label_set_text(s_time, time_str);
    }
}
void travel_ui_show_sync_success(const char *msg) {
    if (s_status) {
        lv_label_set_text(s_status, msg ? msg : "新行程已就绪");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0x2E7D32), 0);
    }
}
void travel_ui_refresh(const travel_content_t *content, const travel_model_t *model,
                       travel_net_status_t network, int battery,
                       const travel_stamp_record_t *stamps,
                       const travel_custom_schedule_t *custom) {
    const travel_place_t *place = &content->places[model->place];
    bool in_passport = (model->page == TRAVEL_PASSPORT || model->page == TRAVEL_STAMP_ANIM);
    bool in_maint = (model->page == TRAVEL_MAINTENANCE);
    bool parent = in_passport || in_maint;

    visible(s_background, !parent);
    visible(s_koala, !parent);
    visible(s_dialog, !parent);
    visible(s_parent, parent);

    visible(s_stamp_box, in_passport);
    visible(s_stamp_info, in_passport);
    visible(s_stamp_time, in_passport);
    visible(s_stamp_page, in_passport);

    visible(s_parent_text, in_maint);
    visible(s_status, in_maint);

    const char *hints[3] = {"行程", "探索", (model->page == TRAVEL_TASK) ? "发现" : "确认"};

    if (in_passport) {
        lv_obj_set_style_text_font(s_title, &travel_ui_font_36, 0);
        lv_label_set_text(s_title, "探索手记");

        size_t total_tasks = (custom && custom->is_custom && custom->task_count > 0)
                           ? custom->task_count : place->task_count;
        if (total_tasks == 0) total_tasks = 1;

        size_t idx = (model->page == TRAVEL_STAMP_ANIM) ? model->task : (model->selection % total_tasks);
        bool stamped = (model->page == TRAVEL_STAMP_ANIM) || travel_model_is_stamped(stamps, idx);

        const char *task_str = "";
        if (custom && custom->is_custom && custom->task_count > 0) {
            task_str = custom->tasks[idx % custom->task_count];
        } else if (idx < place->task_count) {
            task_str = place->tasks[idx];
        }

        uint32_t color = stamped ? 0xC93B2B : 0x8E9AAF;
        lv_obj_set_style_border_color(s_stamp_box, lv_color_hex(color), 0);
        lv_obj_set_style_border_color(s_stamp_inner, lv_color_hex(color), 0);
        lv_obj_set_style_text_color(s_stamp_header, lv_color_hex(color), 0);
        lv_obj_set_style_text_color(s_stamp_title, lv_color_hex(color), 0);
        lv_obj_set_style_text_color(s_stamp_footer, lv_color_hex(color), 0);

        lv_label_set_text(s_stamp_title, stamped ? "已确认" : "待探索");

        char num_buf[20];
        snprintf(num_buf, sizeof(num_buf), "★ NO. %02u ★", (unsigned)(idx + 1));
        lv_label_set_text(s_stamp_footer, num_buf);

        lv_label_set_text(s_stamp_info, task_str);

        if (stamped && stamps && stamps->timestamps[idx] > 0) {
            char time_buf[24];
            travel_model_format_time(stamps->timestamps[idx], time_buf, sizeof(time_buf));
            lv_label_set_text(s_stamp_time, time_buf);
        } else {
            lv_label_set_text(s_stamp_time, stamped ? "已确认" : "待探索");
        }

        char page_buf[16];
        snprintf(page_buf, sizeof(page_buf), "%u / %u", (unsigned)(idx + 1), (unsigned)total_tasks);
        lv_label_set_text(s_stamp_page, page_buf);

        if (model->page == TRAVEL_STAMP_ANIM) {
            hints[0] = ""; hints[1] = ""; hints[2] = "确认";
        } else {
            hints[0] = "上一"; hints[1] = "下一"; hints[2] = "返回";
        }
    } else if (in_maint) {
        lv_obj_set_style_text_font(s_title, &travel_ui_font_36, 0);
        lv_label_set_text(s_title, "网络设置");
        lv_label_set_text(s_parent_text, "手机连接网络:\nKoala-Travel\n\n打开:\n192.168.4.1");
        lv_obj_set_style_text_color(s_status, lv_color_hex(INK), 0);
        lv_label_set_text(s_status, network_text(network.state));
        hints[0] = ""; hints[1] = ""; hints[2] = "返回";
    } else {
        lv_obj_set_style_text_font(s_title, content->title_font, 0);
        if (custom && custom->is_custom && custom->title[0] != '\0') {
            lv_label_set_text(s_title, custom->title);
        } else {
            lv_label_set_text(s_title, place->title);
        }
        lv_image_set_src(s_background, &place->background);

        const char *text = place->home;
        if (model->page == TRAVEL_GREETING) {
            text = place->greetings[model->greeting % (place->greeting_count ? place->greeting_count : 1)];
        } else if (model->page == TRAVEL_TASK) {
            if (custom && custom->is_custom && custom->task_count > 0) {
                text = custom->tasks[model->task % custom->task_count];
            } else {
                text = place->tasks[model->task % (place->task_count ? place->task_count : 1)];
            }
        } else if (model->page == TRAVEL_REPLY) {
            text = place->reply;
        }
        lv_label_set_text(s_dialog_text, text);
        lv_obj_center(s_dialog_text);
    }

    for (unsigned i = 0; i < 3; ++i) {
        lv_label_set_text(s_hints[i], hints[i]);
        lv_obj_center(s_hints[i]);
    }
    visible(s_battery_fill, battery >= 0);
    lv_obj_set_width(s_battery_fill, travel_battery_fill_width(battery));
    lv_obj_set_style_bg_color(s_battery_fill, lv_color_hex(battery < 20 ? 0xe78b66 : 0x69ac79), 0);
    visible(s_network, network.state == TRAVEL_NET_CONNECTED);
    lv_obj_set_style_bg_color(s_network, lv_color_hex(0x69ac79), 0);
}
