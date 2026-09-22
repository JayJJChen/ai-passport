#include "voice_state_payload.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool ok;
} json_writer_t;

static void append(json_writer_t *writer, const char *format, ...) {
    if (!writer->ok || writer->length >= writer->capacity) {
        writer->ok = false;
        return;
    }
    va_list args;
    va_start(args, format);
    int count = vsnprintf(writer->data + writer->length,
                          writer->capacity - writer->length, format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= writer->capacity - writer->length) {
        writer->ok = false;
        return;
    }
    writer->length += (size_t)count;
}

static void append_json_string(json_writer_t *writer, const char *value) {
    append(writer, "\"");
    for (const unsigned char *cursor = (const unsigned char *)(value ? value : "");
         writer->ok && *cursor; ++cursor) {
        switch (*cursor) {
        case '\"': append(writer, "\\\""); break;
        case '\\': append(writer, "\\\\"); break;
        case '\b': append(writer, "\\b"); break;
        case '\f': append(writer, "\\f"); break;
        case '\n': append(writer, "\\n"); break;
        case '\r': append(writer, "\\r"); break;
        case '\t': append(writer, "\\t"); break;
        default:
            if (*cursor < 0x20) append(writer, "\\u%04x", (unsigned)*cursor);
            else append(writer, "%c", *cursor);
            break;
        }
    }
    append(writer, "\"");
}

size_t voice_state_payload_build(const voice_state_snapshot_t *snapshot,
                                 char *destination, size_t capacity) {
    if (!snapshot || !destination || capacity == 0 ||
        snapshot->current_day >= VOICE_STATE_DAY_COUNT ||
        snapshot->battery_percent < -1 || snapshot->battery_percent > 100) return 0;

    json_writer_t writer = {destination, capacity, 0, true};
    append(&writer, "{\"schema_version\":%u,\"trip_id\":\"western-australia-2026\","
                    "\"content_version\":\"cards-v2\",\"state_revision\":%lu,"
                    "\"current_day\":%u,\"preview_mode\":%s,\"date_state\":",
           VOICE_STATE_SCHEMA_VERSION, (unsigned long)snapshot->state_revision,
           (unsigned)snapshot->current_day, snapshot->preview_mode ? "true" : "false");
    append_json_string(&writer, snapshot->date_state);
    append(&writer, ",\"page\":");
    append_json_string(&writer, snapshot->page);
    append(&writer, ",\"current_day_id\":");
    append_json_string(&writer, snapshot->current_day_id);
    if (snapshot->next_reminder == VOICE_STATE_NO_REMINDER) {
        append(&writer, ",\"next_reminder\":null");
    } else {
        append(&writer, ",\"next_reminder\":%u", (unsigned)snapshot->next_reminder);
    }
    append(&writer, ",\"completion_masks\":[");
    for (size_t day = 0; day < VOICE_STATE_DAY_COUNT; ++day)
        append(&writer, "%s%u", day ? "," : "", (unsigned)snapshot->completion_masks[day]);
    append(&writer, "],\"reminder_counts\":[");
    for (size_t day = 0; day < VOICE_STATE_DAY_COUNT; ++day)
        append(&writer, "%s%u", day ? "," : "", (unsigned)snapshot->reminder_counts[day]);
    append(&writer, "],\"battery_percent\":");
    if (snapshot->battery_percent < 0) append(&writer, "null");
    else append(&writer, "%d", snapshot->battery_percent);
    append(&writer, ",\"device_time\":%lld}", (long long)snapshot->device_time);

    if (!writer.ok) {
        destination[0] = '\0';
        return 0;
    }
    return writer.length;
}
