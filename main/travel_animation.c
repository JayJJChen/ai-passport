#include "travel_animation.h"

#include <stddef.h>

typedef struct {
    const uint8_t *frames;
    uint8_t count;
    uint16_t frame_ms;
} motion_spec_t;

static const uint8_t WAVE_FRAMES[] = {0, 1, 2, 3, 2, 1, 0};
static const uint8_t NOD_FRAMES[] = {4, 5, 6, 5, 7, 7};
static const uint8_t POINT_RIGHT_FRAMES[] = {8, 9, 10, 10, 9, 11, 11};
static const uint8_t POINT_LEFT_FRAMES[] = {12, 13, 14, 14, 13, 15, 15};
static const uint8_t WALK_FRAMES[] = {16, 17, 18, 19, 16, 17, 18, 19};

static motion_spec_t motion_spec(travel_motion_t motion) {
    switch (motion) {
    case TRAVEL_MOTION_WAVE:
        return (motion_spec_t){WAVE_FRAMES, sizeof(WAVE_FRAMES), 250};
    case TRAVEL_MOTION_NOD:
        return (motion_spec_t){NOD_FRAMES, sizeof(NOD_FRAMES), 250};
    case TRAVEL_MOTION_POINT_RIGHT:
        return (motion_spec_t){POINT_RIGHT_FRAMES, sizeof(POINT_RIGHT_FRAMES), 250};
    case TRAVEL_MOTION_POINT_LEFT:
        return (motion_spec_t){POINT_LEFT_FRAMES, sizeof(POINT_LEFT_FRAMES), 250};
    case TRAVEL_MOTION_WALK:
        return (motion_spec_t){WALK_FRAMES, sizeof(WALK_FRAMES), 200};
    case TRAVEL_MOTION_NONE:
    default:
        return (motion_spec_t){NULL, 0, 0};
    }
}

void travel_animation_init(travel_animation_t *animation) {
    if (!animation) return;
    *animation = (travel_animation_t){0};
}

void travel_animation_start(travel_animation_t *animation, travel_motion_t motion,
                            uint32_t now_ms) {
    if (!animation) return;
    motion_spec_t spec = motion_spec(motion);
    if (!spec.count) {
        travel_animation_cancel(animation);
        return;
    }
    animation->motion = motion;
    animation->step_started_ms = now_ms;
    animation->step = 0;
    animation->active = true;
}

void travel_animation_cancel(travel_animation_t *animation) {
    if (!animation) return;
    animation->motion = TRAVEL_MOTION_NONE;
    animation->step_started_ms = 0;
    animation->step = 0;
    animation->active = false;
}

bool travel_animation_tick(travel_animation_t *animation, uint32_t now_ms) {
    if (!animation || !animation->active) return false;
    bool changed = false;
    while (animation->active) {
        motion_spec_t spec = motion_spec(animation->motion);
        if (!spec.count || (uint32_t)(now_ms - animation->step_started_ms) < spec.frame_ms) break;
        animation->step_started_ms += spec.frame_ms;
        animation->step++;
        changed = true;
        if (animation->step < spec.count) continue;
        if (animation->motion == TRAVEL_MOTION_WALK) {
            animation->motion = TRAVEL_MOTION_WAVE;
            animation->step = 0;
        } else {
            travel_animation_cancel(animation);
        }
    }
    return changed;
}

uint8_t travel_animation_frame(const travel_animation_t *animation) {
    if (!animation || !animation->active) return 0;
    motion_spec_t spec = motion_spec(animation->motion);
    if (!spec.count || animation->step >= spec.count) return 0;
    return spec.frames[animation->step];
}

bool travel_animation_active(const travel_animation_t *animation) {
    return animation && animation->active;
}

int travel_animation_x(const travel_animation_t *animation) {
    if (!animation || !animation->active || animation->motion != TRAVEL_MOTION_WALK) return 20;
    return -80 + (100 * animation->step) / 7;
}

void travel_motion_tracker_init(travel_motion_tracker_t *tracker) {
    if (!tracker) return;
    *tracker = (travel_motion_tracker_t){0};
}

travel_motion_t travel_motion_for_state(travel_motion_tracker_t *tracker,
                                        const travel_model_t *model,
                                        bool companion_visible,
                                        bool trip_changed,
                                        bool point_left) {
    if (!tracker || !model) return TRAVEL_MOTION_NONE;
    travel_motion_t motion = TRAVEL_MOTION_NONE;
    if (companion_visible) {
        if (model->page == TRAVEL_DAY_TRANSITION &&
            (!tracker->seen || tracker->page != TRAVEL_DAY_TRANSITION || tracker->day != model->day || trip_changed)) {
            motion = TRAVEL_MOTION_WALK;
        } else if (!tracker->seen) {
            motion = TRAVEL_MOTION_WAVE;
        } else if (trip_changed || model->day != tracker->day) {
            motion = TRAVEL_MOTION_WALK;
        } else if (model->page == TRAVEL_FEEDBACK && tracker->page != TRAVEL_FEEDBACK) {
            motion = TRAVEL_MOTION_NOD;
        } else if (model->page == TRAVEL_SCHEDULE &&
                   (tracker->page != TRAVEL_SCHEDULE || model->schedule != tracker->schedule)) {
            motion = TRAVEL_MOTION_WAVE;
        } else if (model->page == TRAVEL_REMINDER &&
                   (tracker->page != TRAVEL_REMINDER || model->reminder != tracker->reminder)) {
            motion = point_left ? TRAVEL_MOTION_POINT_LEFT : TRAVEL_MOTION_POINT_RIGHT;
        }
        tracker->day = model->day;
    }
    tracker->page = model->page;
    tracker->schedule = model->schedule;
    tracker->reminder = model->reminder;
    tracker->seen = true;
    return motion;
}
