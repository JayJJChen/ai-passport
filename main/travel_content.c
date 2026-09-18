#include "travel_content.h"
#include "cJSON.h"
#include <string.h>

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
bool travel_text_check(const char *text, const lv_font_t *font, unsigned columns, unsigned lines) {
    if (!text || !*text || !font) return false;
    unsigned row = 1, col = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        uint32_t c = decode(&p);
        if (c == '\n') { if (!col || ++row > lines) return false; col = 0; continue; }
        if (c < 32 || c == UINT32_MAX || ++col > columns) return false;
        lv_font_glyph_dsc_t glyph = {0};
        if (!lv_font_get_glyph_dsc(font, &glyph, c, 0) || glyph.is_placeholder) return false;
    }
    return col != 0;
}
static const char *string(cJSON *obj, const char *key) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(value) ? value->valuestring : NULL;
}
static bool cards(cJSON *place, const char *key, const lv_font_t *font, const char **out, size_t *count) {
    cJSON *array = cJSON_GetObjectItemCaseSensitive(place, key);
    int n = cJSON_GetArraySize(array);
    if (!cJSON_IsArray(array) || n < 1 || n > TRAVEL_MAX_CARDS) return false;
    for (int i = 0; i < n; ++i) {
        cJSON *value = cJSON_GetArrayItem(array, i);
        if (!cJSON_IsString(value) || !travel_text_check(value->valuestring, font, 5, 2)) return false;
        out[i] = value->valuestring;
    }
    *count = (size_t)n; return true;
}
void travel_content_close(travel_content_t *content) {
    if (content->body_font) lv_binfont_destroy(content->body_font);
    if (content->title_font) lv_binfont_destroy(content->title_font);
    cJSON_Delete(content->json);
    memset(content, 0, sizeof(*content));
}
bool travel_content_open(travel_content_t *content, const void *data, size_t size) {
    memset(content, 0, sizeof(*content));
    travel_pack_t pack;
    travel_pack_file_t manifest, body_font, title_font;
    if (!travel_pack_open(&pack, data, size) ||
        !travel_pack_find(&pack, "manifest.json", &manifest) || manifest.size > 8192 ||
        manifest.data[manifest.size - 1] != 0 || strlen((const char *)manifest.data) != manifest.size - 1 ||
        !travel_pack_find(&pack, "font28.bin", &body_font) || body_font.size > 16384 ||
        !travel_pack_find(&pack, "font36.bin", &title_font) || title_font.size > 4096) return false;
    /* The binary-font loader is used only for locally generated, CRC-verified packs. */
    content->body_font = lv_binfont_create_from_buffer((void *)body_font.data, (uint32_t)body_font.size);
    content->title_font = lv_binfont_create_from_buffer((void *)title_font.data, (uint32_t)title_font.size);
    if (!content->body_font || !content->title_font || content->body_font->line_height > 48 || content->title_font->line_height > 64) goto fail;
    cJSON *root = cJSON_ParseWithLengthOpts((const char *)manifest.data, manifest.size, NULL, true);
    content->json = root;
    if (!root) goto fail;
    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    cJSON *places = cJSON_GetObjectItemCaseSensitive(root, "places");
    int n = cJSON_GetArraySize(places);
    if (!cJSON_IsNumber(version) || version->valuedouble != 1 || !cJSON_IsArray(places) || n < 1 || n > TRAVEL_MAX_PLACES) goto fail;
    for (int i = 0; i < n; ++i) {
        cJSON *obj = cJSON_GetArrayItem(places, i);
        travel_place_t *place = &content->places[i];
        place->id = string(obj, "id"); place->title = string(obj, "title");
        place->home = string(obj, "home"); place->reply = string(obj, "reply");
        const char *background = string(obj, "background");
        travel_pack_file_t image;
        if (!place->id || !*place->id || strlen(place->id) > 20 ||
            !travel_text_check(place->title, content->title_font, 4, 1) ||
            !travel_text_check(place->home, content->body_font, 5, 2) ||
            !travel_text_check(place->reply, content->body_font, 5, 2) ||
            !cards(obj, "greetings", content->body_font, place->greetings, &place->greeting_count) ||
            !cards(obj, "tasks", content->body_font, place->tasks, &place->task_count) ||
            !travel_pack_find(&pack, background, &image) || image.size != 240 * 320 * 2) goto fail;
        for (int j = 0; j < i; ++j) if (!strcmp(content->places[j].id, place->id)) goto fail;
        place->background = (lv_image_dsc_t){
            .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = 240, .h = 320, .stride = 480},
            .data_size = (uint32_t)image.size, .data = image.data,
        };
    }
    content->place_count = (size_t)n;
    return true;
fail:
    travel_content_close(content); return false;
}
