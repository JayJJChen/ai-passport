#include "travel_ui.h"
#include <stdio.h>

LV_FONT_DECLARE(travel_ui_font_24);
LV_FONT_DECLARE(lv_font_montserrat_14);
extern const uint8_t koala_frames_start[] __asm__("_binary_koala_frames_start");
extern const uint8_t koala_frames_end[] __asm__("_binary_koala_frames_end");

static lv_obj_t *s_screen, *s_background, *s_koala, *s_title, *s_time, *s_mode;
static lv_obj_t *s_dialog, *s_dialog_text, *s_selector, *s_selector_text, *s_hints[3];
static lv_obj_t *s_battery, *s_battery_fill;
static lv_image_dsc_t s_koala_frames[TRAVEL_KOALA_FRAME_COUNT];
static travel_animation_t s_animation;
static travel_motion_tracker_t s_motion_tracker;
static lv_timer_t *s_animation_timer;
static const uint32_t INK = 0x123565, CREAM = 0xfff6df;

static void visible(lv_obj_t *obj, bool show) {
    if (show) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, int radius) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
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

static void set_koala_frame(void) {
    uint8_t frame = travel_animation_frame(&s_animation);
    if (frame >= TRAVEL_KOALA_FRAME_COUNT) frame = 0;
    lv_image_set_src(s_koala, &s_koala_frames[frame]);
    lv_obj_set_x(s_koala, travel_animation_x(&s_animation));
}

static void animation_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (travel_animation_tick(&s_animation, lv_tick_get())) set_koala_frame();
    if (!travel_animation_active(&s_animation)) lv_timer_pause(s_animation_timer);
}

void travel_ui_play_motion(travel_motion_t motion) {
    if (!s_koala || !s_animation_timer) return;
    travel_animation_start(&s_animation, motion, lv_tick_get());
    set_koala_frame();
    if (travel_animation_active(&s_animation)) {
        lv_timer_reset(s_animation_timer);
        lv_timer_resume(s_animation_timer);
    } else {
        lv_timer_pause(s_animation_timer);
    }
}

static void update_motion(const travel_model_t *model, bool selector) {
    travel_motion_t motion = travel_motion_for_state(&s_motion_tracker, model, !selector, false,
                                                       lv_rand(0, 1) != 0);
    if (selector) {
        travel_animation_cancel(&s_animation);
        set_koala_frame();
        lv_timer_pause(s_animation_timer);
    } else if (motion != TRAVEL_MOTION_NONE) {
        travel_ui_play_motion(motion);
    }
}

void travel_ui_create(const travel_content_t *content) {
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(CREAM), 0);

    s_background = lv_image_create(s_screen);
    lv_obj_set_pos(s_background, 0, 0);
    s_koala = lv_image_create(s_screen);
    size_t atlas_size = (size_t)(koala_frames_end - koala_frames_start);
    LV_ASSERT_MSG(atlas_size == TRAVEL_KOALA_FRAME_COUNT * TRAVEL_KOALA_FRAME_BYTES,
                  "koala frame atlas has an unexpected size");
    for (size_t i = 0; i < TRAVEL_KOALA_FRAME_COUNT; ++i) {
        s_koala_frames[i] = (lv_image_dsc_t){
            .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8,
                       .w = TRAVEL_KOALA_FRAME_WIDTH, .h = TRAVEL_KOALA_FRAME_HEIGHT,
                       .stride = TRAVEL_KOALA_FRAME_WIDTH * 2u},
            .data_size = TRAVEL_KOALA_FRAME_BYTES,
            .data = koala_frames_start + i * TRAVEL_KOALA_FRAME_BYTES,
        };
    }
    travel_animation_init(&s_animation);
    travel_motion_tracker_init(&s_motion_tracker);
    lv_image_set_src(s_koala, &s_koala_frames[0]);
    lv_obj_set_pos(s_koala, 20, 84);
    s_animation_timer = lv_timer_create(animation_timer_cb, 50, NULL);
    lv_timer_pause(s_animation_timer);

    s_title = label(s_screen, content->title_font, 108);
    lv_obj_set_pos(s_title, 16, 12);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_LEFT, 0);
    s_time = label(s_screen, &lv_font_montserrat_14, 58);
    lv_obj_set_pos(s_time, 128, 8);
    lv_obj_set_style_text_align(s_time, LV_TEXT_ALIGN_RIGHT, 0);
    s_mode = label(s_screen, content->body_font, 58);
    lv_obj_set_pos(s_mode, 128, 25);
    lv_obj_set_style_text_align(s_mode, LV_TEXT_ALIGN_RIGHT, 0);

    s_dialog = panel(s_screen, 8, 230, 180, 82, 20);
    s_dialog_text = label(s_dialog, content->body_font, 168);
    lv_obj_set_style_text_line_space(s_dialog_text, 36 - content->body_font->line_height, 0);

    s_selector = panel(s_screen, 8, 70, 180, 242, 16);
    s_selector_text = label(s_selector, content->body_font, 168);
    lv_obj_set_pos(s_selector_text, 6, 26);
    lv_obj_set_style_text_line_space(s_selector_text, 12, 0);

    for (unsigned i = 0; i < 3; ++i) {
        lv_obj_t *hint = panel(s_screen, 184, travel_key_center(i, 320) - 22, 54, 44, 12);
        s_hints[i] = label(hint, &travel_ui_font_24, 54);
        lv_obj_center(s_hints[i]);
    }
    s_battery = panel(s_screen, 194, 10, TRAVEL_BATTERY_GAUGE_W, TRAVEL_BATTERY_GAUGE_H, 2);
    lv_obj_set_style_border_color(s_battery, lv_color_hex(INK), 0);
    lv_obj_set_style_border_width(s_battery, TRAVEL_BATTERY_BORDER_W, 0);
    s_battery_fill = panel(s_battery, TRAVEL_BATTERY_INNER_GAP, TRAVEL_BATTERY_INNER_GAP,
                           TRAVEL_BATTERY_FILL_MAX_W, TRAVEL_BATTERY_FILL_H, 0);
    visible(s_selector, false);
    lv_screen_load(s_screen);
}

void travel_ui_set_time(const char *time_str) {
    if (s_time && time_str) lv_label_set_text(s_time, time_str);
}

static uint8_t preferred_reminder(const travel_day_t *day, const travel_model_t *model,
                                  const travel_completion_t *completion,
                                  int current_minute, bool clock_valid) {
    int16_t minutes[TRAVEL_MAX_REMINDERS] = {-1, -1, -1, -1};
    for (size_t i = 0; i < day->reminder_count; ++i) minutes[i] = day->reminders[i].minute_of_day;
    uint8_t mask = completion && model->day < TRAVEL_MAX_DAYS ? completion->completed[model->day] : 0;
    return travel_model_next_reminder(minutes, day->reminder_count, mask, clock_valid,
                                      model->preview || model->date_state != TRAVEL_DATE_ACTIVE,
                                      current_minute);
}

void travel_ui_refresh(const travel_content_t *content, const travel_model_t *model,
                       const travel_completion_t *completion, int battery,
                       int current_minute, bool clock_valid) {
    size_t day_index = model->day < content->day_count ? model->day : 0;
    const travel_day_t *day = &content->days[day_index];
    bool selector = model->page == TRAVEL_DAY_SELECT;
    update_motion(model, selector);

    visible(s_background, !selector);
    visible(s_koala, !selector);
    visible(s_dialog, !selector);
    visible(s_selector, selector);
    lv_image_set_src(s_background, &day->background);

    const char *hints[3] = {"今天", "提醒", ""};
    const char *mode = "";
    if (model->date_state == TRAVEL_DATE_PREVIEW || model->date_state == TRAVEL_DATE_BEFORE ||
        model->date_state == TRAVEL_DATE_UNKNOWN) mode = "预览";

    lv_obj_set_style_text_font(s_title, content->title_font, 0);
    lv_label_set_text(s_title, day->title);

    if (selector) {
        lv_obj_set_style_text_font(s_title, content->body_font, 0);
        lv_label_set_text(s_title, "选择日期");
        char selection[80];
        if (model->selection == 0) {
            snprintf(selection, sizeof(selection), "自动模式\n\n按日期显示");
        } else {
            size_t selected = model->selection - 1;
            int key = travel_model_date_key_for_day(selected);
            snprintf(selection, sizeof(selection), "10月%u日\n\n第%u天", (unsigned)(key % 100), (unsigned)(selected + 1));
        }
        lv_label_set_text(s_selector_text, selection);
        lv_obj_center(s_selector_text);
        hints[0] = "上一"; hints[1] = "下一"; hints[2] = "确定";
        mode = "";
    } else {
        const char *text = day->home;
        uint8_t reminder = preferred_reminder(day, model, completion, current_minute, clock_valid);
        if (model->page == TRAVEL_HOME) {
            text = reminder == TRAVEL_DAY_NONE ? "今天事项\n都完成啦" : day->reminders[reminder].text;
        } else if (model->page == TRAVEL_SCHEDULE) {
            text = day->schedule[model->schedule % TRAVEL_SCHEDULE_CARD_COUNT];
            static const char *kinds[] = {"路线", "活动", "住宿"};
            mode = kinds[model->schedule % TRAVEL_SCHEDULE_CARD_COUNT];
            hints[0] = model->schedule + 1 >= TRAVEL_SCHEDULE_CARD_COUNT ? "返回" : "下一";
            hints[2] = "返回";
        } else if (model->page == TRAVEL_REMINDER) {
            size_t index = model->reminder < day->reminder_count ? model->reminder : 0;
            text = day->reminders[index].text;
            mode = "提醒";
            hints[0] = "今天"; hints[1] = "下一"; hints[2] = "完成";
        } else if (model->page == TRAVEL_FEEDBACK) {
            text = day->reply;
            mode = "完成";
            hints[0] = hints[1] = hints[2] = "";
        } else if (model->page == TRAVEL_DAY_TRANSITION) {
            static char transition[40];
            snprintf(transition, sizeof(transition), "第%u天，\n出发啦！", (unsigned)(day_index + 1));
            text = transition;
            hints[0] = hints[1] = hints[2] = "";
            mode = "";
        }
        if (model->date_state == TRAVEL_DATE_AFTER && model->page == TRAVEL_HOME) {
            lv_obj_set_style_text_font(s_title, content->body_font, 0);
            lv_label_set_text(s_title, "旅程完成");
        }
        lv_label_set_text(s_dialog_text, text);
        lv_obj_center(s_dialog_text);
    }

    lv_label_set_text(s_mode, mode);
    for (unsigned i = 0; i < 3; ++i) {
        lv_label_set_text(s_hints[i], hints[i]);
        lv_obj_center(s_hints[i]);
    }
    visible(s_battery_fill, battery >= 0);
    lv_obj_set_width(s_battery_fill, travel_battery_fill_width(battery));
    lv_obj_set_style_bg_color(s_battery_fill, lv_color_hex(battery < 20 ? 0xe78b66 : 0x69ac79), 0);
}
