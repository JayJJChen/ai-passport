#include "travel_model.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static travel_action_t key(travel_model_t *m, travel_input_t input, uint32_t now) {
    return travel_model_input(m, input, TRAVEL_SCHEDULE_CARD_COUNT, 4, now);
}

int main(void) {
    travel_model_t model;
    travel_model_init(&model);
    assert(model.page == TRAVEL_HOME && model.day == 0 && !model.preview);

    travel_saved_state_t saved, loaded;
    travel_saved_state_defaults(&saved);
    assert(saved.version == TRAVEL_SAVED_STATE_VERSION && saved.last_walk_day == TRAVEL_DAY_NONE && saved.state_revision == 0);
    saved.preview_mode = 1; saved.preview_day = 6; saved.last_walk_day = 5;
    saved.completion.completed[2] = 0x05;
    assert(travel_saved_state_import(&loaded, &saved, sizeof(saved)));
    travel_model_restore(&model, &loaded);
    assert(model.preview && model.day == 6 && model.date_state == TRAVEL_DATE_PREVIEW);
    assert(loaded.completion.completed[2] == 0 && loaded.last_walk_day == 5);
    saved.version++;
    assert(!travel_saved_state_import(&loaded, &saved, sizeof(saved)));
    assert(loaded.version == TRAVEL_SAVED_STATE_VERSION && loaded.last_walk_day == TRAVEL_DAY_NONE);
    saved.version = TRAVEL_SAVED_STATE_VERSION; saved.completion.completed[0] = 0x80;
    assert(!travel_saved_state_import(&loaded, &saved, sizeof(saved)));
    assert(!travel_saved_state_import(&loaded, &saved, sizeof(saved) - 1));
    struct {
        uint32_t version;
        travel_completion_t completion;
        uint8_t preview_mode, preview_day, last_walk_day, reserved;
    } old = {1, {{0}}, 1, 3, 2, 0};
    old.completion.completed[1] = 0x03;
    assert(travel_saved_state_import(&loaded, &old, sizeof(old)));
    assert(loaded.version == TRAVEL_SAVED_STATE_VERSION && loaded.preview_mode == 1 &&
           loaded.preview_day == 3 && loaded.completion.completed[1] == 0 && loaded.state_revision == 0);
    saved.completion.completed[0] = 3; saved.version = 2;
    assert(travel_saved_state_import(&loaded, &saved, sizeof(saved)));
    assert(loaded.completion.completed[0] == 0 && loaded.preview_day == saved.preview_day);
    travel_model_init(&model);

    key(&model, TRAVEL_UP, 0); assert(model.page == TRAVEL_SCHEDULE && model.schedule == 0);
    key(&model, TRAVEL_UP, 0); assert(model.schedule == 1);
    key(&model, TRAVEL_UP, 0); assert(model.schedule == 2);
    key(&model, TRAVEL_UP, 0); assert(model.page == TRAVEL_HOME && model.day == 0);
    key(&model, TRAVEL_DOWN, 0); assert(model.page == TRAVEL_REMINDER && model.reminder == 0);
    key(&model, TRAVEL_DOWN, 0); assert(model.reminder == 1);
    assert(key(&model, TRAVEL_OK, UINT32_MAX - 999) == TRAVEL_COMPLETE_REMINDER);
    assert(model.page == TRAVEL_FEEDBACK);
    key(&model, TRAVEL_UP, 0); assert(model.page == TRAVEL_FEEDBACK);
    assert(!travel_model_tick(&model, 1999));
    assert(travel_model_tick(&model, 2000));
    assert(model.page == TRAVEL_HOME);

    travel_completion_t completion = {0};
    model.day = 2; model.reminder = 3;
    assert(travel_model_complete(&model, &completion));
    assert(!travel_model_complete(&model, &completion));
    assert(travel_model_is_complete(&completion, 2, 3));
    assert(!travel_model_is_complete(&completion, 1, 3));
    assert(!travel_model_is_complete(&completion, 2, 2));

    model.day = 0; model.preview = false; model.page = TRAVEL_HOME;
    assert(key(&model, TRAVEL_UP_LONG, 0) == TRAVEL_NO_ACTION);
    assert(model.page == TRAVEL_DAY_SELECT && model.selection == 0);
    key(&model, TRAVEL_DOWN, 0); assert(model.selection == 1);
    assert(key(&model, TRAVEL_OK, 0) == TRAVEL_SAVE_SELECTION);
    assert(model.preview && model.day == 0 && model.page == TRAVEL_HOME);
    key(&model, TRAVEL_UP_LONG, 0); assert(model.selection == 1);
    key(&model, TRAVEL_DOWN, 0); assert(model.selection == 2);
    assert(key(&model, TRAVEL_OK, 100) == TRAVEL_DAY_CHANGED);
    assert(model.preview && model.day == 1 && model.page == TRAVEL_DAY_TRANSITION);
    key(&model, TRAVEL_UP, 200); assert(model.page == TRAVEL_DAY_TRANSITION);
    assert(key(&model, TRAVEL_DOWN_LONG, 200) == TRAVEL_ENTER_DEEP_SLEEP);
    assert(!travel_model_tick(&model, 3449));
    assert(travel_model_tick(&model, 3450));
    assert(model.page == TRAVEL_HOME);
    key(&model, TRAVEL_UP_LONG, 4000);
    while (model.selection != 0) key(&model, TRAVEL_UP, 4000);
    assert(key(&model, TRAVEL_OK, 4000) == TRAVEL_SAVE_SELECTION);
    assert(!model.preview);

    travel_date_state_t state;
    assert(travel_model_day_for_date(2026, 10, 1, &state) == 0 && state == TRAVEL_DATE_BEFORE);
    for (int day = 2; day <= 12; ++day) {
        assert(travel_model_day_for_date(2026, 10, day, &state) == day - 2);
        assert(state == TRAVEL_DATE_ACTIVE);
    }
    assert(travel_model_day_for_date(2026, 10, 13, &state) == 10 && state == TRAVEL_DATE_AFTER);
    assert(travel_model_date_key_for_day(0) == 20261002);
    assert(travel_model_date_key_for_day(10) == 20261012);

    int16_t times[] = {600, -1, 900, -1};
    assert(travel_model_next_reminder(times, 4, 0, true, false, 700) == 0);
    assert(travel_model_next_reminder(times, 4, 1, true, false, 700) == 1);
    assert(travel_model_next_reminder(times, 4, 3, true, false, 700) == 3);
    assert(travel_model_next_reminder(times, 4, 11, true, false, 700) == 2);
    assert(travel_model_next_reminder(times, 4, 0, false, false, 700) == 0);
    assert(travel_model_next_reminder(times, 4, 1, true, true, 700) == 1);
    assert(travel_model_next_reminder(times, 4, 15, true, false, 700) == TRAVEL_DAY_NONE);

    assert(travel_key_center(0, 320) == 53 && travel_key_center(1, 320) == 160 && travel_key_center(2, 320) == 267);
    assert(travel_key_center(3, 320) == -1);
    assert(travel_backlight_level(29999, false) == 75);
    assert(travel_backlight_level(30000, false) == 20);
    assert(travel_backlight_level(60000, false) == 10);
    assert(travel_backlight_level(120000, true) == 75);
    assert(!travel_model_should_sleep(119999) && travel_model_should_sleep(120000));
    assert(TRAVEL_BATTERY_FILL_MAX_W == 23 && TRAVEL_BATTERY_FILL_H == 6);
    assert(travel_battery_fill_width(19) == 4 && travel_battery_fill_width(50) == 11);
    assert(travel_battery_fill_width(120) == 23);

    char date[12], time_text[12];
    travel_model_format_day(6, date, sizeof(date)); assert(strcmp(date, "10/08") == 0);
    travel_model_format_time(15 * 60 + 30, true, time_text, sizeof(time_text));
    assert(strcmp(time_text, "15:30") == 0);
    travel_model_format_time(23 * 60 + 59, true, time_text, sizeof(time_text));
    assert(strcmp(time_text, "23:59") == 0);
    travel_model_format_time(15 * 60 + 30, false, time_text, sizeof(time_text));
    assert(strcmp(time_text, "--:--") == 0);
    travel_model_format_time(24 * 60, true, time_text, sizeof(time_text));
    assert(strcmp(time_text, "--:--") == 0);
    assert(!travel_model_clock_valid(0));
    assert(travel_model_clock_valid(1789812000));
    puts("Western Australia date, reminder, persistence-state and interaction tests: PASS");
}
