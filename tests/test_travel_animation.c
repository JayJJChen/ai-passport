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
    assert(travel_animation_frame(&animation) == 0);

    travel_animation_start(&animation, TRAVEL_MOTION_WAVE, 1000);
    assert(travel_animation_active(&animation));
    expect_frame(&animation, 1249, 0);
    expect_frame(&animation, 1250, 1);
    expect_frame(&animation, 1500, 2);
    expect_frame(&animation, 1750, 3);
    expect_frame(&animation, 2000, 2);
    expect_frame(&animation, 2250, 1);
    expect_frame(&animation, 2500, 0);
    expect_frame(&animation, 2750, 0);
    assert(!travel_animation_active(&animation));

    travel_animation_start(&animation, TRAVEL_MOTION_NOD, UINT32_MAX - 99u);
    expect_frame(&animation, 150u, 5);
    expect_frame(&animation, 400u, 6);

    travel_animation_start(&animation, TRAVEL_MOTION_POINT_RIGHT, 0);
    expect_frame(&animation, 500, 10);
    travel_animation_start(&animation, TRAVEL_MOTION_POINT_LEFT, 500);
    assert(travel_animation_frame(&animation) == 12);
    expect_frame(&animation, 1000, 14);

    travel_animation_start(&animation, TRAVEL_MOTION_WALK, 0);
    expect_frame(&animation, 1599, 19);
    expect_frame(&animation, 1600, 0);
    assert(animation.motion == TRAVEL_MOTION_WAVE);
    expect_frame(&animation, 1850, 1);
    expect_frame(&animation, 3350, 0);
    assert(!travel_animation_active(&animation));

    travel_animation_start(&animation, TRAVEL_MOTION_WAVE, 10);
    travel_animation_start(&animation, TRAVEL_MOTION_NOD, 20);
    assert(animation.motion == TRAVEL_MOTION_NOD);
    assert(travel_animation_frame(&animation) == 4);
    travel_animation_cancel(&animation);
    assert(!travel_animation_active(&animation));
    assert(travel_animation_frame(&animation) == 0);

    travel_motion_tracker_t tracker;
    travel_model_t model;
    travel_motion_tracker_init(&tracker);
    travel_model_init(&model);
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_WAVE);
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_NONE);
    model.page = TRAVEL_GREETING;
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_WAVE);
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_NONE);
    model.greeting = 1;
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_WAVE);
    model.page = TRAVEL_TASK;
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_POINT_RIGHT);
    model.task = 1;
    assert(travel_motion_for_state(&tracker, &model, true, false, true) == TRAVEL_MOTION_POINT_LEFT);
    model.page = TRAVEL_STAMP_ANIM;
    assert(travel_motion_for_state(&tracker, &model, false, false, false) == TRAVEL_MOTION_NONE);
    model.page = TRAVEL_HOME;
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_NOD);
    model.page = TRAVEL_MAINTENANCE;
    assert(travel_motion_for_state(&tracker, &model, false, true, false) == TRAVEL_MOTION_NONE);
    model.page = TRAVEL_HOME;
    assert(travel_motion_for_state(&tracker, &model, true, true, false) == TRAVEL_MOTION_WALK);
    model.place = 1;
    assert(travel_motion_for_state(&tracker, &model, true, false, false) == TRAVEL_MOTION_WALK);

    puts("Koala animation timing: PASS");
    return 0;
}
