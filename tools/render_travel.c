/* Software-only 240x320 verification with the pinned LVGL and production UI. */
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
            char normalized[1024];
            unsigned length = 0;
            for (unsigned i = 0; text[i]; ++i) {
                if (text[i] == '\n' && (!i || text[i - 1] == '\n')) continue;
                assert(length + 1 < sizeof(normalized));
                normalized[length++] = text[i];
            }
            normalized[length] = 0;
            assert(travel_text_check(normalized, font, 220, 20));
            lv_area_t box, parent;
            lv_obj_get_coords(object, &box);
            lv_obj_get_coords(lv_obj_get_parent(object), &parent);
            assert(box.x1 >= parent.x1 && box.y1 >= parent.y1 && box.x2 <= parent.x2 && box.y2 <= parent.y2);
            assert(box.x1 >= 0 && box.y1 >= 0 && box.x2 < 240 && box.y2 < 320);
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
        check_labels(lv_obj_get_child(object, i), hidden);
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    const uint16_t *source = (const uint16_t *)pixels;
    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = area->x1; x <= area->x2; ++x) {
            uint16_t pixel = *source++;
            frame[y * 240 + x] = bsp_display_pixel_outside_rounded_rect(x, y, 240, 320, 30) ? 0 : pixel;
        }
    }
    lv_display_flush_ready(display);
}

static void save_frame(const char *directory, const char *name) {
    lv_refr_now(NULL);
    check_labels(lv_screen_active(), false);
    char filename[1024];
    snprintf(filename, sizeof(filename), "%s/%s.ppm", directory, name);
    FILE *fp = fopen(filename, "wb");
    assert(fp);
    fprintf(fp, "P6\n240 320\n255\n");
    for (unsigned i = 0; i < 240 * 320; ++i) {
        unsigned r = (frame[i] >> 11) & 31, g = (frame[i] >> 5) & 63, b = frame[i] & 31;
        uint8_t rgb[3] = {
            (uint8_t)((r << 3) | (r >> 2)),
            (uint8_t)((g << 2) | (g >> 4)),
            (uint8_t)((b << 3) | (b >> 2)),
        };
        fwrite(rgb, 1, 3, fp);
    }
    fclose(fp);
}

static void advance_animation(uint32_t milliseconds) {
    lv_tick_inc(milliseconds);
    lv_timer_handler();
}

static void refresh_static(const travel_content_t *content, travel_model_t *model,
                           const travel_completion_t *completion, int minute, bool clock_valid) {
    travel_ui_refresh(content, model, completion, 82, minute, clock_valid);
    travel_ui_play_motion(TRAVEL_MOTION_NONE);
}

static bool parse_datetime(const char *value, int *year, int *month, int *day, int *minute) {
    int hour, min;
    char tail;
    if (sscanf(value, "%d-%d-%dT%d:%d%c", year, month, day, &hour, &min, &tail) != 5 ||
        *month < 1 || *month > 12 || *day < 1 || *day > 31 || hour < 0 || hour > 23 || min < 0 || min > 59) return false;
    *minute = hour * 60 + min;
    return true;
}

static void render_day(const char *output, const travel_content_t *content, travel_model_t *model,
                       travel_completion_t *completion, int minute, bool clock_valid) {
    char name[64];
    model->page = TRAVEL_HOME;
    memset(completion, 0, sizeof(*completion));
    refresh_static(content, model, completion, minute, clock_valid);
    snprintf(name, sizeof(name), "day%02u-home", (unsigned)(model->day + 1));
    save_frame(output, name);

    for (size_t i = 0; i < TRAVEL_SCHEDULE_CARD_COUNT; ++i) {
        model->page = TRAVEL_SCHEDULE;
        model->schedule = (uint8_t)i;
        refresh_static(content, model, completion, minute, clock_valid);
        snprintf(name, sizeof(name), "day%02u-schedule%u", (unsigned)(model->day + 1), (unsigned)(i + 1));
        save_frame(output, name);
    }
    const travel_day_t *day = &content->days[model->day];
    for (size_t i = 0; i < day->reminder_count; ++i) {
        model->page = TRAVEL_REMINDER;
        model->reminder = (uint8_t)i;
        refresh_static(content, model, completion, minute, clock_valid);
        snprintf(name, sizeof(name), "day%02u-reminder%u", (unsigned)(model->day + 1), (unsigned)(i + 1));
        save_frame(output, name);
    }
    model->page = TRAVEL_FEEDBACK;
    refresh_static(content, model, completion, minute, clock_valid);
    snprintf(name, sizeof(name), "day%02u-completed", (unsigned)(model->day + 1));
    save_frame(output, name);
    completion->completed[model->day] = 0x0f;
    model->page = TRAVEL_HOME;
    refresh_static(content, model, completion, minute, clock_valid);
    snprintf(name, sizeof(name), "day%02u-all-done", (unsigned)(model->day + 1));
    save_frame(output, name);
}

int main(int argc, char **argv) {
    if (argc != 3 && argc != 5) {
        fprintf(stderr, "Usage: %s <cards.klp> <output-dir> [--datetime YYYY-MM-DDTHH:MM]\n", argv[0]);
        return 2;
    }
    const char *datetime = NULL;
    if (argc == 5) {
        if (strcmp(argv[3], "--datetime")) return 2;
        datetime = argv[4];
    }
    FILE *fp = fopen(argv[1], "rb"); assert(fp);
    fseek(fp, 0, SEEK_END);
    size_t size = (size_t)ftell(fp);
    rewind(fp);
    uint8_t *data = malloc(size); assert(data);
    assert(fread(data, 1, size, fp) == size);
    fclose(fp);

    lv_init();
    lv_display_t *display = lv_display_create(240, 320);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    travel_content_t content;
    assert(travel_content_open(&content, data, size));
    assert(content.day_count == TRAVEL_MAX_DAYS);
    assert(!travel_text_check("龘", content.body_font, TRAVEL_BODY_MAX_PX, 2));
    assert(!travel_text_check("\xc0\xaf", content.body_font, TRAVEL_BODY_MAX_PX, 2));
    assert(!travel_text_check("字字字字字字字", content.body_font, TRAVEL_BODY_MAX_PX, 2));

    travel_model_t model;
    travel_completion_t completion = {0};
    travel_model_init(&model);
    travel_ui_create(&content);
    int minute = 12 * 60;
    if (datetime) {
        int year, month, day;
        if (!parse_datetime(datetime, &year, &month, &day, &minute)) return 2;
        model.day = (uint8_t)travel_model_day_for_date(year, month, day, &model.date_state);
        render_day(argv[2], &content, &model, &completion, minute, true);
    } else {
        for (size_t day = 0; day < content.day_count; ++day) {
            model.day = (uint8_t)day;
            model.date_state = TRAVEL_DATE_PREVIEW;
            model.preview = true;
            render_day(argv[2], &content, &model, &completion, minute, false);
        }
    }

    model.preview = false;
    model.page = TRAVEL_DAY_SELECT;
    model.selection = 0;
    refresh_static(&content, &model, &completion, minute, true);
    save_frame(argv[2], "date-selector-auto");
    model.selection = 7;
    refresh_static(&content, &model, &completion, minute, true);
    save_frame(argv[2], "date-selector-day07");

    model.day = 6;
    model.page = TRAVEL_DAY_TRANSITION;
    model.page_since = 0;
    travel_ui_refresh(&content, &model, &completion, 82, minute, true);
    save_frame(argv[2], "transition-first");
    advance_animation(800);
    save_frame(argv[2], "transition-middle");
    advance_animation(800);
    save_frame(argv[2], "transition-arrived");
    advance_animation(250);
    save_frame(argv[2], "transition-wave");

    printf("Western Australia UI rendering: PASS, %u days, body line=%u, title line=%u\n",
        (unsigned)content.day_count, content.body_font->line_height, content.title_font->line_height);
    lv_obj_delete(lv_screen_active());
    travel_content_close(&content);
    free(data);
    return 0;
}
