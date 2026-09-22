#pragma once
#include "lvgl.h"
#include "travel_pack.h"

#include "travel_model.h"
#include "travel_progress.h"

#define TRAVEL_TITLE_MAX_PX 108
#define TRAVEL_BODY_MAX_PX 168

typedef struct {
    const char *id, *text;
    bool optional;
} travel_activity_t;

typedef struct {
    const char *id, *date, *title, *home, *reply;
    const char *schedule[TRAVEL_SCHEDULE_CARD_COUNT];
    travel_activity_t activities[TRAVEL_MAX_ACTIVITIES];
    size_t activity_count;
    bool compact_companion;
    lv_image_dsc_t background;
} travel_day_t;
typedef struct {
    travel_day_t days[TRAVEL_MAX_DAYS];
    size_t day_count;
    const char *trip_id, *content_version;
    const lv_font_t *body_font, *title_font;
    void *json;
} travel_content_t;
/* Call while owning the LVGL lock, after lv_init(), before creating the UI. */
bool travel_content_open(travel_content_t *content, const void *data, size_t size);
/* Shared production selection path. Keep the selected buffer alive until content closes.
 * used_builtin is true only when the built-in buffer was successfully selected. */
bool travel_content_open_with_fallback(travel_content_t *content,
                                      const void *preferred, size_t preferred_size,
                                      const void *builtin, size_t builtin_size,
                                      bool *used_builtin);
void travel_content_close(travel_content_t *content);
bool travel_text_check(const char *text, const lv_font_t *font, unsigned max_pixels, unsigned lines);
