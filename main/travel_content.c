#include "travel_content.h"
#include "cJSON.h"
#include <string.h>

LV_FONT_DECLARE(travel_ui_font_24);
LV_FONT_DECLARE(travel_ui_font_36);

/* Strict Unicode decoding also rejects malformed/split UTF-8 in a replacement pack. */
static uint32_t decode(const unsigned char **cursor) {
    const unsigned char *p = *cursor;
    uint32_t value = *p++;
    if (value < 0x80) { *cursor = p; return value; }
    unsigned n; uint32_t minimum;
    if (value >= 0xc2 && value <= 0xdf) { n = 1; minimum = 0x80; value &= 31; }
    else if (value >= 0xe0 && value <= 0xef) { n = 2; minimum = 0x800; value &= 15; }
    else if (value >= 0xf0 && value <= 0xf4) { n = 3; minimum = 0x10000; value &= 7; }
    else return UINT32_MAX;
    while (n--) { if ((*p & 0xc0) != 0x80) return UINT32_MAX; value = value << 6 | (*p++ & 63); }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return UINT32_MAX;
    *cursor = p; return value;
}
bool travel_text_check(const char *text, const lv_font_t *font, unsigned max_pixels, unsigned lines) {
    if (!text || !*text || !font) return false;
    unsigned row = 1, width = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        uint32_t c = decode(&p);
        if (c == '\n') { if (!width || ++row > lines) return false; width = 0; continue; }
        if (c < 32 || c == UINT32_MAX) return false;
        lv_font_glyph_dsc_t glyph = {0};
        if (!lv_font_get_glyph_dsc(font, &glyph, c, 0) || glyph.is_placeholder) return false;
        width += glyph.adv_w;
        if (width > max_pixels * 16U) return false;
    }
    return width != 0;
}
static const char *string(cJSON *obj, const char *key) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(value) ? value->valuestring : NULL;
}
static bool schedule(cJSON *day, const lv_font_t *font, const char **out) {
    cJSON *array = cJSON_GetObjectItemCaseSensitive(day, "schedule");
    int n = cJSON_GetArraySize(array);
    if (!cJSON_IsArray(array) || n != TRAVEL_SCHEDULE_CARD_COUNT) return false;
    for (int i = 0; i < n; ++i) {
        cJSON *value = cJSON_GetArrayItem(array, i);
        if (!cJSON_IsString(value) || !travel_text_check(value->valuestring, font, TRAVEL_BODY_MAX_PX, 2)) return false;
        out[i] = value->valuestring;
    }
    return true;
}
static int parse_time(const char *value) {
    if (!value) return -1;
    if (strlen(value) != 5 || value[2] != ':' || value[0] < '0' || value[0] > '2' ||
        value[1] < '0' || value[1] > '9' || value[3] < '0' || value[3] > '5' ||
        value[4] < '0' || value[4] > '9') return -2;
    int hour = (value[0] - '0') * 10 + value[1] - '0';
    return hour < 24 ? hour * 60 + (value[3] - '0') * 10 + value[4] - '0' : -2;
}
static bool reminders(cJSON *day, const lv_font_t *font, travel_reminder_t *out, size_t *count) {
    cJSON *array = cJSON_GetObjectItemCaseSensitive(day, "reminders");
    int n = cJSON_GetArraySize(array);
    if (!cJSON_IsArray(array) || n < 1 || n > TRAVEL_MAX_REMINDERS) return false;
    for (int i = 0; i < n; ++i) {
        cJSON *item = cJSON_GetArrayItem(array, i);
        const char *text = string(item, "text");
        cJSON *at_item = cJSON_GetObjectItemCaseSensitive(item, "at");
        const char *at = cJSON_IsString(at_item) ? at_item->valuestring : NULL;
        int minute = parse_time(at);
        if (!cJSON_IsObject(item) || !travel_text_check(text, font, TRAVEL_BODY_MAX_PX, 2) || minute == -2) return false;
        out[i] = (travel_reminder_t){.text = text, .minute_of_day = (int16_t)minute};
    }
    *count = (size_t)n;
    return true;
}
void travel_content_close(travel_content_t *content) {
    cJSON_Delete(content->json);
    memset(content, 0, sizeof(*content));
}
bool travel_content_open(travel_content_t *content, const void *data, size_t size) {
    memset(content, 0, sizeof(*content));
    content->body_font = &travel_ui_font_24;
    content->title_font = &travel_ui_font_36;
    travel_pack_t pack;
    travel_pack_file_t manifest;
    if (!travel_pack_open(&pack, data, size) ||
        !travel_pack_find(&pack, "manifest.json", &manifest) || manifest.size > 32768 ||
        manifest.data[manifest.size - 1] != 0 || strlen((const char *)manifest.data) != manifest.size - 1) return false;
    cJSON *root = cJSON_ParseWithLengthOpts((const char *)manifest.data, manifest.size, NULL, true);
    content->json = root;
    if (!root) goto fail;
    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    cJSON *days = cJSON_GetObjectItemCaseSensitive(root, "days");
    int n = cJSON_GetArraySize(days);
    if (!cJSON_IsNumber(version) || version->valuedouble != 2 || !cJSON_IsArray(days) || n != TRAVEL_MAX_DAYS) goto fail;
    for (int i = 0; i < n; ++i) {
        cJSON *obj = cJSON_GetArrayItem(days, i);
        travel_day_t *day = &content->days[i];
        day->id = string(obj, "id"); day->date = string(obj, "date"); day->title = string(obj, "title");
        day->home = string(obj, "home"); day->reply = string(obj, "reply");
        const char *background = string(obj, "background");
        travel_pack_file_t image;
        if (!day->id || !*day->id || strlen(day->id) > 20 || !day->date || strlen(day->date) != 10 ||
            !travel_text_check(day->title, content->title_font, TRAVEL_TITLE_MAX_PX, 1) ||
            !travel_text_check(day->home, content->body_font, TRAVEL_BODY_MAX_PX, 2) ||
            !travel_text_check(day->reply, content->body_font, TRAVEL_BODY_MAX_PX, 2) ||
            !schedule(obj, content->body_font, day->schedule) ||
            !reminders(obj, content->body_font, day->reminders, &day->reminder_count) ||
            !travel_pack_find(&pack, background, &image) || image.size != 240 * 320 * 2) goto fail;
        for (int j = 0; j < i; ++j) if (!strcmp(content->days[j].id, day->id)) goto fail;
        day->background = (lv_image_dsc_t){
            .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = 240, .h = 320, .stride = 480},
            .data_size = (uint32_t)image.size, .data = image.data,
        };
    }
    content->day_count = (size_t)n;
    return true;
fail:
    travel_content_close(content); return false;
}
