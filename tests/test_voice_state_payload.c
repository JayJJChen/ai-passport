#include "voice_state_payload.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    voice_state_snapshot_t state = {
        .state_revision = 42,
        .progress_revision = 8,
        .current_activity_id = "d08_quokka",
        .current_day = 6,
        .preview_mode = true,
        .date_state = "preview",
        .page = "home",
        .current_day_id = "day07_fremantle",
        .next_reminder = 2,
        .battery_percent = 87,
        .device_time = 1791459000,
    };
    state.completion_masks[0] = 7;
    state.completion_masks[6] = 3;
    for (size_t i = 0; i < VOICE_STATE_DAY_COUNT; ++i) state.reminder_counts[i] = i == 6 ? 4 : 3;

    char payload[768];
    size_t length = voice_state_payload_build(&state, payload, sizeof(payload));
    assert(length == strlen(payload));
    assert(strstr(payload, "\"schema_version\":2"));
    assert(strstr(payload, "\"state_revision\":42"));
    assert(strstr(payload, "\"content_version\":\"wa-child-v1\""));
    assert(strstr(payload, "\"progress_revision\":8"));
    assert(strstr(payload, "\"current_activity_id\":\"d08_quokka\""));
    assert(strstr(payload, "\"current_day_id\":\"day07_fremantle\""));
    assert(strstr(payload, "\"completion_masks\":[7,0,0,0,0,0,3,0,0,0,0]"));
    assert(strstr(payload, "\"reminder_counts\":[3,3,3,3,3,3,4,3,3,3,3]"));
    assert(strstr(payload, "\"battery_percent\":87"));

    state.next_reminder = VOICE_STATE_NO_REMINDER;
    state.battery_percent = -1;
    state.current_day_id = "quoted\"id";
    length = voice_state_payload_build(&state, payload, sizeof(payload));
    assert(length && strstr(payload, "\"next_reminder\":null"));
    assert(strstr(payload, "\"battery_percent\":null"));
    assert(strstr(payload, "quoted\\\"id"));
    assert(voice_state_payload_build(&state, payload, 32) == 0 && payload[0] == '\0');
    state.current_day = VOICE_STATE_DAY_COUNT;
    assert(voice_state_payload_build(&state, payload, sizeof(payload)) == 0);
    puts("Voice state payload tests: PASS");
}
