#include "travel_ui.h"
#include <stdio.h>

LV_FONT_DECLARE(travel_ui_font_24);
LV_FONT_DECLARE(lv_font_montserrat_14);
extern const uint8_t koala_frames_start[] __asm__("_binary_koala_frames_start");
extern const uint8_t koala_frames_end[] __asm__("_binary_koala_frames_end");

static lv_obj_t *s_screen, *s_background, *s_koala, *s_title, *s_time, *s_date, *s_mode;
static lv_obj_t *s_dialog, *s_dialog_text, *s_selector, *s_selector_text, *s_hints[3];
static lv_obj_t *s_battery, *s_battery_fill, *s_mode_panel;
static lv_image_dsc_t s_koala_frames[TRAVEL_KOALA_FRAME_COUNT];
static travel_animation_t s_animation;
static travel_motion_tracker_t s_motion_tracker;
static lv_timer_t *s_animation_timer;
static int s_koala_base_x = 20, s_koala_base_y = 84;
static bool s_compact;
static travel_voice_status_t s_voice_status = TRAVEL_VOICE_OFFLINE;
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
    int offset = travel_animation_x(&s_animation) - 20;
    if (s_compact) offset = offset * 2 / 3;
    lv_obj_set_pos(s_koala, s_koala_base_x + offset, s_koala_base_y);
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

    /* Opaque cream surfaces keep the fixed ink labels readable over every daily scene.
     * They sit behind the existing text positions; the compact companion and key hints do not move. */
    (void)panel(s_screen, 72, 2, 156, 22, 10);   /* time, date and battery, inside the rounded top edge */
    (void)panel(s_screen, 8, 25, 116, 36, 10);   /* title, including the date selector's body font */
    s_mode_panel = panel(s_screen, 125, 33, 61, 37, 10);

    s_title = label(s_screen, content->title_font, 108);
    lv_obj_set_pos(s_title, 16, 28);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_LEFT, 0);
    s_time = label(s_screen, &lv_font_montserrat_14, 46);
    lv_obj_set_pos(s_time, 78, 6);
    lv_obj_set_style_text_align(s_time, LV_TEXT_ALIGN_RIGHT, 0);
    s_date = label(s_screen, &lv_font_montserrat_14, 56);
    lv_obj_set_pos(s_date, 130, 6);
    lv_obj_set_style_text_align(s_date, LV_TEXT_ALIGN_RIGHT, 0);
    s_mode = label(s_screen, content->body_font, 56);
    lv_obj_set_pos(s_mode, 128, 36);
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

void travel_ui_set_voice_status(travel_voice_status_t status) {
    s_voice_status = status;
}


void travel_ui_refresh(const travel_content_t *content, const travel_model_t *model,
                       const travel_progress_t *progress, int battery,
                       int current_minute, bool clock_valid) {
    size_t day_index = model->day < content->day_count ? model->day : 0;
    const travel_day_t *day = &content->days[day_index];
    bool selector = model->page == TRAVEL_DAY_SELECT;
    s_compact = day->compact_companion;
    s_koala_base_x = s_compact ? 8 : 20; s_koala_base_y = s_compact ? 132 : 84;
    lv_image_set_pivot(s_koala, 0, 0);
    lv_image_set_scale(s_koala, s_compact ? 171 : 256);
    update_motion(model, selector);
    set_koala_frame();
    char time_text[6], date[6];
    travel_model_format_time(current_minute, clock_valid, time_text, sizeof(time_text));
    travel_model_format_day(day_index, date, sizeof(date));
    lv_label_set_text(s_time, time_text);
    lv_label_set_text(s_date, date);

    visible(s_background, !selector);
    visible(s_koala, !selector);
    visible(s_dialog, !selector);
    visible(s_selector, selector);
    lv_image_set_src(s_background, &day->background);

    const char *hints[3] = {"今天", "活动", "对话"};
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
        if (model->page == TRAVEL_HOME) {
            text = day->home;
            switch (s_voice_status) {
            case TRAVEL_VOICE_CONFIGURING:
                text = "手机连接热点\n打开配网页面"; mode = "配网"; break;
            case TRAVEL_VOICE_SYNCING:
                text = "连接语音\n请稍候"; mode = "语音"; break;
            case TRAVEL_VOICE_LISTENING:
                text = "请说话\n再按发送"; mode = "语音"; break;
            case TRAVEL_VOICE_THINKING:
                text = "正在思考\n请稍候"; mode = "语音"; break;
            case TRAVEL_VOICE_SPEAKING:
                text = "正在回答\n再按打断"; mode = "语音"; break;
            case TRAVEL_VOICE_ERROR:
                text = "语音暂不可用\n稍后再试"; mode = "离线"; break;
            case TRAVEL_VOICE_OFFLINE:
                mode = "离线"; break;
            case TRAVEL_VOICE_READY:
                break;
            }
        } else if (model->page == TRAVEL_SCHEDULE) {
            text = day->schedule[model->schedule % TRAVEL_SCHEDULE_CARD_COUNT];
            static const char *kinds[] = {"路线", "活动", "住宿"};
            mode = kinds[model->schedule % TRAVEL_SCHEDULE_CARD_COUNT];
            hints[0] = model->schedule + 1 >= TRAVEL_SCHEDULE_CARD_COUNT ? "返回" : "下一";
            hints[2] = "返回";
        } else if (model->page == TRAVEL_REMINDER) {
            size_t index = model->reminder < day->activity_count ? model->reminder : 0;
            text = day->activities[index].text;
            int entry = travel_progress_find(progress, day->activities[index].id);
            const travel_progress_entry_t *p = entry >= 0 ? &progress->entries[entry] : NULL;
            mode = p && p->conflict ? "确认" : progress && progress->writes_paused ? "更新" :
                   p && p->queued ? "待传" : p && p->server.status == TRAVEL_ACTIVITY_COMPLETED ? "已玩" :
                   p && p->server.status == TRAVEL_ACTIVITY_SKIPPED ? "跳过" : "活动";
            hints[0] = "今天"; hints[1] = "下一"; hints[2] = "玩过";
        } else if (model->page == TRAVEL_FEEDBACK) {
            size_t index = model->reminder < day->activity_count ? model->reminder : 0;
            int entry = travel_progress_find(progress, day->activities[index].id);
            const travel_progress_entry_t *p = entry >= 0 ? &progress->entries[entry] : NULL;
            text = p && p->conflict ? "记录有变化\n问考拉确认" :
                   progress && progress->writes_paused ? "行程待更新\n稍后再记录" :
                   p && p->queued ? "已记在这里\n联网后同步" :
                   p && p->server.status == TRAVEL_ACTIVITY_COMPLETED ? "已经记下啦\n继续去玩吧" :
                   "暂时没存好\n请再试一次";
            mode = p && p->queued ? "待传" : p && p->server.status == TRAVEL_ACTIVITY_COMPLETED ? "已玩" : "确认";
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
    visible(s_mode_panel, mode[0] != 0);
    for (unsigned i = 0; i < 3; ++i) {
        lv_label_set_text(s_hints[i], hints[i]);
        lv_obj_center(s_hints[i]);
    }
    visible(s_battery_fill, battery >= 0);
    lv_obj_set_width(s_battery_fill, travel_battery_fill_width(battery));
    lv_obj_set_style_bg_color(s_battery_fill, lv_color_hex(battery < 20 ? 0xe78b66 : 0x69ac79), 0);
}
