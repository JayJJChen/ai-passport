#include "travel_model.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

static travel_action_t key(travel_model_t *m, travel_input_t k, uint32_t now) {
    return travel_model_input(m, k, 6, 3, 3, now);
}
int main(void) {
    travel_model_t m; travel_model_init(&m);
    assert(m.page == TRAVEL_HOME && m.place == 0);
    key(&m, TRAVEL_UP, 0); assert(m.page == TRAVEL_GREETING && m.greeting == 0);
    key(&m, TRAVEL_UP, 0); assert(m.greeting == 1);
    key(&m, TRAVEL_UP, 0); key(&m, TRAVEL_UP, 0); assert(m.greeting == 0);
    key(&m, TRAVEL_DOWN, 0); assert(m.page == TRAVEL_TASK && m.task == 0);
    key(&m, TRAVEL_DOWN, 0); assert(m.task == 1);
    assert(key(&m, TRAVEL_OK, UINT32_MAX - 999) == TRAVEL_STAMP_CURRENT && m.page == TRAVEL_STAMP_ANIM);
    assert(!travel_model_tick(&m, 1999)); assert(travel_model_tick(&m, 2000)); assert(m.page == TRAVEL_HOME);
    travel_stamp_record_t stamps = {0};
    m.task = 1;
    travel_model_stamp(&m, &stamps, 1720000000);
    assert(travel_model_is_stamped(&stamps, 1));
    assert(!travel_model_is_stamped(&stamps, 0));
    assert(!travel_model_is_stamped(&stamps, 2));
    assert(stamps.timestamps[1] == 1720000000);
    assert(key(&m, TRAVEL_OK_LONG, 0) == TRAVEL_NO_ACTION && m.page == TRAVEL_PASSPORT);
    assert(m.selection == 0);
    key(&m, TRAVEL_DOWN, 0); assert(m.selection == 1);
    key(&m, TRAVEL_DOWN, 0); assert(m.selection == 2);
    key(&m, TRAVEL_DOWN, 0); assert(m.selection == 0);
    key(&m, TRAVEL_UP, 0); assert(m.selection == 2);
    assert(key(&m, TRAVEL_OK, 0) == TRAVEL_NO_ACTION && m.page == TRAVEL_HOME);
    assert(key(&m, TRAVEL_UP_LONG, 0) == TRAVEL_START_SOFTAP && m.page == TRAVEL_MAINTENANCE);
    assert(key(&m, TRAVEL_OK, 0) == TRAVEL_STOP_SOFTAP && m.page == TRAVEL_HOME);
    assert(key(&m, TRAVEL_DOWN_LONG, 0) == TRAVEL_ENTER_DEEP_SLEEP);
    assert(travel_key_center(0, 320) == 53 && travel_key_center(1, 320) == 160 && travel_key_center(2, 320) == 267);
    assert(travel_key_center(3, 320) == -1);
    assert(travel_backlight_level(29999, false) == 75);
    assert(travel_backlight_level(30000, false) == 20);
    assert(travel_backlight_level(59999, false) == 20);
    assert(travel_backlight_level(60000, false) == 10);
    assert(travel_backlight_level(UINT32_MAX, false) == 10);
    assert(travel_backlight_level(120000, true) == 75);
    assert(!travel_model_should_sleep(0));
    assert(!travel_model_should_sleep(119999));
    assert(travel_model_should_sleep(120000));
    assert(travel_model_should_sleep(300000));
    assert(TRAVEL_PASSPORT == TRAVEL_SETTINGS);
    char tbuf[32] = {0};
    travel_model_format_time(1789812000, tbuf, sizeof(tbuf));
    assert(tbuf[2] == '/' && tbuf[5] == ' ' && tbuf[8] == ':');
    assert(TRAVEL_BATTERY_BORDER_W * 2 + TRAVEL_BATTERY_INNER_GAP * 2 + TRAVEL_BATTERY_FILL_MAX_W == TRAVEL_BATTERY_GAUGE_W);
    assert(TRAVEL_BATTERY_BORDER_W * 2 + TRAVEL_BATTERY_INNER_GAP * 2 + TRAVEL_BATTERY_FILL_H == TRAVEL_BATTERY_GAUGE_H);
    assert(TRAVEL_BATTERY_FILL_MAX_W == 23 && TRAVEL_BATTERY_FILL_H == 6);
    assert(travel_battery_fill_width(-1) == 0);
    assert(travel_battery_fill_width(0) == 0);
    assert(travel_battery_fill_width(19) == 4);
    assert(travel_battery_fill_width(50) == 11);
    assert(travel_battery_fill_width(100) == 23);
    assert(travel_battery_fill_width(120) == 23);
    puts("Travel interaction/timeout/parent-confirmation tests: PASS");
}
