#include "travel_ui.h"
#include <stdio.h>

LV_FONT_DECLARE(travel_ui_font_24);
LV_FONT_DECLARE(travel_ui_font_36);
extern const lv_image_dsc_t koala_sprite;
static lv_obj_t *s_screen, *s_background, *s_koala, *s_title, *s_dialog, *s_dialog_text;
static lv_obj_t *s_parent, *s_parent_text, *s_status, *s_hints[3], *s_battery, *s_battery_fill, *s_network;
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
    s_title = label(s_screen, content->title_font, 168); lv_obj_set_pos(s_title, 10, 18);
    s_dialog = panel(s_screen, 8, 230, 172, 82, 20);
    s_dialog_text = label(s_dialog, content->body_font, 156);
    lv_obj_set_style_text_line_space(s_dialog_text, 36 - content->body_font->line_height, 0);
    s_parent = panel(s_screen, 8, 80, 172, 232, 16);
    s_parent_text = label(s_parent, &travel_ui_font_24, 160);
    lv_obj_set_style_text_line_space(s_parent_text, 32 - travel_ui_font_24.line_height, 0);
    lv_obj_set_pos(s_parent_text, 6, 4);
    s_status = label(s_parent, &travel_ui_font_24, 160); lv_obj_set_pos(s_status, 6, 179);
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
    s_network = panel(s_screen, 178, 12, 8, 8, 4);
    lv_screen_load(s_screen);
}
static const char *network_text(travel_net_state_t state) {
    switch (state) {
    case TRAVEL_NET_CONNECTING: return "连接中";
    case TRAVEL_NET_CONNECTED: return "已联网";
    case TRAVEL_NET_PAIRING: return "等待手机";
    case TRAVEL_NET_PHONE: return "手机已连接";
    case TRAVEL_NET_FAILED: return "连接失败";
    case TRAVEL_NET_TIMEOUT: return "配网超时";
    default: return "未联网";
    }
}
void travel_ui_refresh(const travel_content_t *content, const travel_model_t *model,
                       travel_net_status_t network, int battery) {
    const travel_place_t *place = &content->places[model->place];
    bool parent = model->page >= TRAVEL_SETTINGS;
    visible(s_background, !parent); visible(s_koala, !parent); visible(s_dialog, !parent); visible(s_parent, parent);
    lv_obj_set_style_text_font(s_title, parent ? &travel_ui_font_36 : content->title_font, 0);
    lv_label_set_text(s_title, place->title);
    lv_image_set_src(s_background, &place->background);
    const char *text = place->home;
    if (model->page == TRAVEL_GREETING) text = place->greetings[model->greeting];
    if (model->page == TRAVEL_TASK) text = place->tasks[model->task];
    if (model->page == TRAVEL_REPLY) text = place->reply;
    lv_label_set_text(s_dialog_text, text); lv_obj_center(s_dialog_text);
    const char *hints[3] = {"招呼", "探索", model->page == TRAVEL_TASK ? "发现" : "回应"};
    lv_label_set_text(s_parent_text, ""); lv_label_set_text(s_status, "");
    if (parent) {
        hints[0] = "上一"; hints[1] = "下一"; hints[2] = "确定";
        lv_label_set_text(s_title, "家长设置");
        if (model->page == TRAVEL_SETTINGS) {
            static const char *options[] = {"连接配网", "忘记网络", "目的地", "返回"};
            char menu[160];
            snprintf(menu, sizeof(menu), "%s%s\n%s%s\n%s%s\n%s%s",
                model->selection == 0 ? "> " : "  ", options[0], model->selection == 1 ? "> " : "  ", options[1],
                model->selection == 2 ? "> " : "  ", options[2], model->selection == 3 ? "> " : "  ", options[3]);
            lv_label_set_text(s_parent_text, menu);
            lv_label_set_text(s_status, network_text(network.state));
        } else if (model->page == TRAVEL_PROVISION) {
            lv_label_set_text(s_title, "蓝牙配网");
            lv_label_set_text(s_parent_text, "微信小程序\n蓝牙配网-\nFoloToy AI\nPASSPORT");
            lv_label_set_text(s_status, network_text(network.state));
            hints[0] = ""; hints[1] = ""; hints[2] = "返回";
        } else if (model->page == TRAVEL_FORGET_CONFIRM) {
            lv_label_set_text(s_title, "确认");
            lv_label_set_text(s_parent_text, "忘记网络吗？\n\n确定清除\n上下取消");
            hints[0] = "取消"; hints[1] = "取消";
        } else if (model->page == TRAVEL_PLACES) {
            /* City names use the pack's 36 px font, including future destinations. */
            lv_label_set_text(s_title, "目的地");
            lv_obj_set_style_text_font(s_parent_text, content->title_font, 0);
            lv_label_set_text(s_parent_text, content->places[model->selection].title);
        }
    }
    if (model->page != TRAVEL_PLACES) lv_obj_set_style_text_font(s_parent_text, &travel_ui_font_24, 0);
    for (unsigned i = 0; i < 3; ++i) { lv_label_set_text(s_hints[i], hints[i]); lv_obj_center(s_hints[i]); }
    visible(s_battery_fill, battery >= 0);
    lv_obj_set_width(s_battery_fill, travel_battery_fill_width(battery));
    lv_obj_set_style_bg_color(s_battery_fill, lv_color_hex(battery < 20 ? 0xe78b66 : 0x69ac79), 0);
    visible(s_network, network.state == TRAVEL_NET_CONNECTED);
    lv_obj_set_style_bg_color(s_network, lv_color_hex(0x69ac79), 0);
}
