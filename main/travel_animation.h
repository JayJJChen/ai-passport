#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "travel_model.h"

#define TRAVEL_KOALA_FRAME_COUNT 20u
#define TRAVEL_KOALA_FRAME_WIDTH 144u
#define TRAVEL_KOALA_FRAME_HEIGHT 144u
#define TRAVEL_KOALA_FRAME_BYTES (TRAVEL_KOALA_FRAME_WIDTH * TRAVEL_KOALA_FRAME_HEIGHT * 3u)

typedef enum {
    TRAVEL_MOTION_NONE = 0,
    TRAVEL_MOTION_WAVE,
    TRAVEL_MOTION_NOD,
    TRAVEL_MOTION_POINT_RIGHT,
    TRAVEL_MOTION_POINT_LEFT,
    TRAVEL_MOTION_WALK,
} travel_motion_t;

typedef struct {
    travel_motion_t motion;
    uint32_t step_started_ms;
    uint8_t step;
    bool active;
} travel_animation_t;

typedef struct {
    travel_page_t page;
    size_t day;
    size_t schedule;
    size_t reminder;
    bool seen;
} travel_motion_tracker_t;

void travel_animation_init(travel_animation_t *animation);
void travel_animation_start(travel_animation_t *animation, travel_motion_t motion,
                            uint32_t now_ms);
void travel_animation_cancel(travel_animation_t *animation);
bool travel_animation_tick(travel_animation_t *animation, uint32_t now_ms);
uint8_t travel_animation_frame(const travel_animation_t *animation);
bool travel_animation_active(const travel_animation_t *animation);
/* Horizontal koala position: enters from off-screen left during the walk. */
int travel_animation_x(const travel_animation_t *animation);
void travel_motion_tracker_init(travel_motion_tracker_t *tracker);
travel_motion_t travel_motion_for_state(travel_motion_tracker_t *tracker,
                                        const travel_model_t *model,
                                        bool companion_visible,
                                        bool trip_changed,
                                        bool point_left);
