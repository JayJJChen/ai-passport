#include "travel_animation.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void expect_frame(travel_animation_t *animation, uint32_t now, uint8_t frame) {
    travel_animation_tick(animation, now);
    assert(travel_animation_frame(animation) == frame);
}

int main(void) {
    travel_animation_t animation;
    travel_animation_init(&animation);
    assert(!travel_animation_active(&animation));
    travel_animation_start(&animation, TRAVEL_MOTION_WAVE, 1000);
    expect_frame(&animation, 1250, 1);
    expect_frame(&animation, 2500, 0);
    expect_frame(&animation, 2750, 0);
    assert(!travel_animation_active(&animation));

    travel_animation_start(&animation, TRAVEL_MOTION_NOD, UINT32_MAX - 99u);
    expect_frame(&animation, 150u, 5);
    travel_animation_start(&animation, TRAVEL_MOTION_POINT_RIGHT, 0);
    expect_frame(&animation, 500, 10);
    travel_animation_start(&animation, TRAVEL_MOTION_POINT_LEFT, 500);
    expect_frame(&animation, 1000, 14);

    travel_animation_start(&animation, TRAVEL_MOTION_WALK, 0);
    assert(travel_animation_x(&animation) == -80);
    expect_frame(&animation, 800, 16);
    assert(travel_animation_x(&animation) < 0);
    expect_frame(&animation, 1400, 19);
    assert(travel_animation_x(&animation) == 20);
    expect_frame(&animation, 1600, 0);
    assert(animation.motion == TRAVEL_MOTION_WAVE && travel_animation_x(&animation) == 20);
    expect_frame(&animation, 3350, 0);
    assert(!travel_animation_active(&animation));

    travel_motion_tracker_t tracker;
    travel_model_t model;
    travel_motion_tracker_init(&tracker);
    travel_model_init(&model);
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_WAVE);
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_NONE);
    model.page = TRAVEL_SCHEDULE;
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_WAVE);
    model.schedule = 1;
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_WAVE);
    model.page = TRAVEL_REMINDER;
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_POINT_RIGHT);
    model.reminder = 1;
    assert(travel_motion_for_state(&tracker, &model, true, false, true) == TRAVEL_MOTION_POINT_LEFT);
    model.page = TRAVEL_FEEDBACK;
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_NOD);
    model.page = TRAVEL_HOME;
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_NONE);
    model.day = 1; model.page = TRAVEL_DAY_TRANSITION;
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_WALK);
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_NONE);
    model.page = TRAVEL_HOME;
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_NONE);
    puts("Koala two-cycle walk, horizontal entry, wave and state choreography: PASS");
}
