#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOICE_STATE_SCHEMA_VERSION 2U
#define VOICE_STATE_DAY_COUNT 11U
#define VOICE_STATE_NO_REMINDER 0xffU

typedef struct {
    uint32_t state_revision, progress_revision;
    const char *current_activity_id;
    uint8_t current_day;
    bool preview_mode;
    const char *date_state;
    const char *page;
    const char *current_day_id;
    uint8_t next_reminder;
    uint8_t completion_masks[VOICE_STATE_DAY_COUNT];
    uint8_t reminder_counts[VOICE_STATE_DAY_COUNT];
    int battery_percent;
    int64_t device_time;
} voice_state_snapshot_t;

/* Serializes the complete device-side travel state. Returns bytes written, or 0
 * when the destination is too small or the snapshot is invalid. */
size_t voice_state_payload_build(const voice_state_snapshot_t *snapshot,
                                 char *destination, size_t capacity);

#ifdef __cplusplus
}
#endif
