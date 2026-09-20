#pragma once
#include "lvgl.h"
#include "travel_pack.h"

#include "travel_model.h"

#define TRAVEL_TITLE_MAX_PX 108
#define TRAVEL_BODY_MAX_PX 168

typedef struct {
    const char *text;
    int16_t minute_of_day;
} travel_reminder_t;

typedef struct {
    const char *id, *date, *title, *home, *reply;
    const char *schedule[TRAVEL_SCHEDULE_CARD_COUNT];
    travel_reminder_t reminders[TRAVEL_MAX_REMINDERS];
    size_t reminder_count;
    lv_image_dsc_t background;
} travel_day_t;
typedef struct {
    travel_day_t days[TRAVEL_MAX_DAYS];
    size_t day_count;
    const lv_font_t *body_font, *title_font;
    void *json;
} travel_content_t;
/* Call while owning the LVGL lock, after lv_init(), before creating the UI. */
bool travel_content_open(travel_content_t *content, const void *data, size_t size);
void travel_content_close(travel_content_t *content);
bool travel_text_check(const char *text, const lv_font_t *font, unsigned max_pixels, unsigned lines);
