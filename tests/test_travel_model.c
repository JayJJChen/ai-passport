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
    key(&m, TRAVEL_OK, UINT32_MAX - 999); assert(m.page == TRAVEL_REPLY);
    assert(!travel_model_tick(&m, 1999)); assert(travel_model_tick(&m, 2000)); assert(m.page == TRAVEL_HOME);
    key(&m, TRAVEL_OK_LONG, 0); assert(m.page == TRAVEL_SETTINGS);
    assert(key(&m, TRAVEL_OK, 0) == TRAVEL_START_PROVISION && m.page == TRAVEL_PROVISION);
    assert(key(&m, TRAVEL_OK_LONG, 0) == TRAVEL_STOP_PROVISION && m.page == TRAVEL_HOME);
    key(&m, TRAVEL_OK_LONG, 0); key(&m, TRAVEL_DOWN, 0); key(&m, TRAVEL_OK, 0);
    assert(m.page == TRAVEL_FORGET_CONFIRM);
    assert(key(&m, TRAVEL_UP, 0) == TRAVEL_NO_ACTION && m.page == TRAVEL_SETTINGS);
    key(&m, TRAVEL_OK, 0); assert(key(&m, TRAVEL_OK, 0) == TRAVEL_FORGET_WIFI);
    m.place = 3; m.selection = 2;
    key(&m, TRAVEL_OK, 0); assert(m.page == TRAVEL_PLACES && m.selection == 3);
    key(&m, TRAVEL_UP, 0); assert(m.selection == 2);
    assert(key(&m, TRAVEL_OK, 0) == TRAVEL_CHANGE_PLACE && m.place == 2 && m.page == TRAVEL_HOME && m.task == 0);
    assert(travel_key_center(0, 320) == 53 && travel_key_center(1, 320) == 160 && travel_key_center(2, 320) == 267);
    assert(travel_key_center(3, 320) == -1);
    assert(travel_backlight_level(29999, false) == 75);
    assert(travel_backlight_level(30000, false) == 20);
    assert(travel_backlight_level(59999, false) == 20);
    assert(travel_backlight_level(60000, false) == 10);
    assert(travel_backlight_level(UINT32_MAX, false) == 10);
    assert(travel_backlight_level(120000, true) == 75);
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
