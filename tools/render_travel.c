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

static uint32_t frame_hash(void) {
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < 240 * 320; ++i) hash = (hash ^ frame[i]) * 16777619u;
    return hash;
}

static void advance_animation(uint32_t milliseconds) {
    lv_tick_inc(milliseconds);
    lv_timer_handler();
}

static void refresh_static(const travel_content_t *content, travel_model_t *model,
                           const travel_progress_t *progress, int minute, bool clock_valid) {
    travel_ui_refresh(content, model, progress, 82, minute, clock_valid);
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
                       travel_progress_t *progress, int minute, bool clock_valid) {
    char name[64];
    model->page = TRAVEL_HOME;

    refresh_static(content, model, progress, minute, clock_valid);
    snprintf(name, sizeof(name), "day%02u-home", (unsigned)(model->day + 1));
    save_frame(output, name);
    uint32_t home_hash = frame_hash();

    for (size_t i = 0; i < TRAVEL_SCHEDULE_CARD_COUNT; ++i) {
        model->page = TRAVEL_SCHEDULE;
        model->schedule = (uint8_t)i;
        refresh_static(content, model, progress, minute, clock_valid);
        snprintf(name, sizeof(name), "day%02u-schedule%u", (unsigned)(model->day + 1), (unsigned)(i + 1));
        save_frame(output, name);
    }
    const travel_day_t *day = &content->days[model->day];
    for (size_t i = 0; i < day->activity_count; ++i) {
        model->page = TRAVEL_REMINDER;
        model->reminder = (uint8_t)i;
        refresh_static(content, model, progress, minute, clock_valid);
        snprintf(name, sizeof(name), "day%02u-reminder%u", (unsigned)(model->day + 1), (unsigned)(i + 1));
        save_frame(output, name);
    }
    model->page = TRAVEL_FEEDBACK;
    refresh_static(content, model, progress, minute, clock_valid);
    snprintf(name, sizeof(name), "day%02u-save-failed", (unsigned)(model->day + 1));
    save_frame(output, name);
    for (size_t i = 0; i < day->activity_count; ++i) {
        int entry = travel_progress_find(progress, day->activities[i].id);
        progress->entries[entry].server.status = TRAVEL_ACTIVITY_COMPLETED;
    }
    model->page = TRAVEL_HOME;
    refresh_static(content, model, progress, minute, clock_valid);
    snprintf(name, sizeof(name), "day%02u-all-done", (unsigned)(model->day + 1));
    save_frame(output, name);
    assert(frame_hash() == home_hash); /* home stays the day theme, never an all-done score */
}

/* Exercise the same selection function used by production content_load(), with real validators. */
static void check_content_fallback(const uint8_t *builtin, size_t size) {
    uint8_t *broken = malloc(size);
    assert(broken && size > TRAVEL_PACK_HEADER_SIZE);
    memcpy(broken, builtin, size);
    broken[size - 5] ^= 1;
    travel_content_t content;
    bool used_builtin = true;
    assert(travel_content_open_with_fallback(&content, builtin, size, broken, size, &used_builtin));
    assert(!used_builtin && content.day_count == TRAVEL_MAX_DAYS);
    travel_content_close(&content);
    assert(travel_content_open_with_fallback(&content, broken, size, builtin, size, &used_builtin));
    assert(used_builtin && content.day_count == TRAVEL_MAX_DAYS && content.days[7].compact_companion);
    size_t activities = 0;
    for (size_t day = 0; day < content.day_count; ++day) {
        activities += content.days[day].activity_count;
        assert((uintptr_t)content.days[day].background.data - (uintptr_t)builtin < size);
    }
    assert(activities == 24);
    travel_content_close(&content);
    assert(!travel_content_open_with_fallback(&content, broken, size, broken, size, &used_builtin));
    assert(!used_builtin && !content.json && !content.day_count);
    assert(travel_content_open_with_fallback(&content, NULL, 0, builtin, size, &used_builtin));
    assert(used_builtin && content.day_count == TRAVEL_MAX_DAYS);
    travel_content_close(&content);
    free(broken);
    puts("Production card selection: preferred / corrupt fallback / both corrupt / absent partition: PASS");
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
    check_content_fallback(data, size);
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
    travel_progress_t progress;
    travel_progress_init(&progress, 1, 2);
    for (size_t d = 0; d < content.day_count; ++d)
        for (size_t a = 0; a < content.days[d].activity_count; ++a)
            assert(travel_progress_add(&progress, content.days[d].activities[a].id));
    travel_model_init(&model);
    travel_ui_create(&content);
    int minute = 12 * 60;
    if (datetime) {
        int year, month, day;
        if (!parse_datetime(datetime, &year, &month, &day, &minute)) return 2;
        model.day = (uint8_t)travel_model_day_for_date(year, month, day, &model.date_state);
        render_day(argv[2], &content, &model, &progress, minute, true);
    } else {
        for (size_t day = 0; day < content.day_count; ++day) {
            model.day = (uint8_t)day;
            model.date_state = TRAVEL_DATE_PREVIEW;
            model.preview = true;
            render_day(argv[2], &content, &model, &progress, minute, false);
        }
    }

    model.preview = false;
    model.page = TRAVEL_DAY_SELECT;
    model.selection = 0;
    refresh_static(&content, &model, &progress, minute, true);
    save_frame(argv[2], "date-selector-auto");
    model.selection = 7;
    refresh_static(&content, &model, &progress, minute, true);
    save_frame(argv[2], "date-selector-day07");

    model.day = 6;
    model.page = TRAVEL_DAY_TRANSITION;
    model.page_since = 0;
    travel_ui_refresh(&content, &model, &progress, 82, minute, true);
    save_frame(argv[2], "transition-first");
    advance_animation(800);
    save_frame(argv[2], "transition-middle");
    advance_animation(800);
    save_frame(argv[2], "transition-arrived");
    advance_animation(250);
    save_frame(argv[2], "transition-wave");

    model.day = 7;
    assert(content.days[7].compact_companion);
    model.page = TRAVEL_DAY_TRANSITION;
    travel_ui_refresh(&content, &model, &progress, 82, minute, true);
    save_frame(argv[2], "quokka-transition-first");
    advance_animation(800); save_frame(argv[2], "quokka-transition-middle");
    advance_animation(800); save_frame(argv[2], "quokka-transition-arrived");
    advance_animation(250); save_frame(argv[2], "quokka-transition-wave");
    model.page = TRAVEL_HOME;
    for (unsigned status = TRAVEL_VOICE_OFFLINE; status <= TRAVEL_VOICE_ERROR; ++status) {
        char name[64];
        travel_ui_set_voice_status((travel_voice_status_t)status);
        refresh_static(&content, &model, &progress, minute, true);
        snprintf(name, sizeof(name), "quokka-voice-%u", status); save_frame(argv[2], name);
    }
    travel_ui_set_voice_status(TRAVEL_VOICE_READY);
    model.page = TRAVEL_REMINDER; model.reminder = 0;
    int entry = travel_progress_find(&progress, content.days[7].activities[0].id);
    travel_progress_entry_t *p = &progress.entries[entry];
    p->queued = true; p->desired_status = TRAVEL_ACTIVITY_COMPLETED;
    refresh_static(&content, &model, &progress, minute, true); save_frame(argv[2], "quokka-pending");
    p->queued = false; p->server.status = TRAVEL_ACTIVITY_SKIPPED;
    refresh_static(&content, &model, &progress, minute, true); save_frame(argv[2], "quokka-skipped");
    p->conflict = true;
    refresh_static(&content, &model, &progress, minute, true); save_frame(argv[2], "quokka-conflict");
    model.page = TRAVEL_FEEDBACK;
    refresh_static(&content, &model, &progress, minute, true); save_frame(argv[2], "quokka-conflict-feedback");
    p->conflict = false; p->server.status = TRAVEL_ACTIVITY_COMPLETED;
    refresh_static(&content, &model, &progress, minute, true); save_frame(argv[2], "quokka-completed-feedback");
    p->server.status = TRAVEL_ACTIVITY_PENDING;
    refresh_static(&content, &model, &progress, minute, true); save_frame(argv[2], "quokka-save-failed");
    p->queued = true;
    refresh_static(&content, &model, &progress, minute, true); save_frame(argv[2], "quokka-pending-feedback");
    progress.writes_paused = true;
    refresh_static(&content, &model, &progress, minute, true); save_frame(argv[2], "quokka-version-mismatch");

    printf("Western Australia UI rendering: PASS, %u days, body line=%u, title line=%u\n",
        (unsigned)content.day_count, content.body_font->line_height, content.title_font->line_height);
    lv_obj_delete(lv_screen_active());
    travel_content_close(&content);
    free(data);
    return 0;
}
