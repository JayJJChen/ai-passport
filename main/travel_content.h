#pragma once
#include "lvgl.h"
#include "travel_pack.h"

#define TRAVEL_MAX_PLACES 6
#define TRAVEL_MAX_CARDS 20
typedef struct {
    const char *id, *title, *home, *reply;
    const char *greetings[TRAVEL_MAX_CARDS], *tasks[TRAVEL_MAX_CARDS];
    size_t greeting_count, task_count;
    lv_image_dsc_t background;
} travel_place_t;
typedef struct {
    travel_place_t places[TRAVEL_MAX_PLACES];
    size_t place_count;
    lv_font_t *body_font, *title_font;
    void *json;
} travel_content_t;
/* Call while owning the LVGL lock, after lv_init(), before creating the UI. */
bool travel_content_open(travel_content_t *content, const void *data, size_t size);
void travel_content_close(travel_content_t *content);
bool travel_text_check(const char *text, const lv_font_t *font, unsigned columns, unsigned lines);
