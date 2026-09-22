#pragma once
#include "travel_animation.h"
#include "travel_content.h"
#include "travel_model.h"
void travel_ui_create(const travel_content_t *content);
void travel_ui_refresh(const travel_content_t *content, const travel_model_t *model,
                       const travel_completion_t *completion, int battery,
                       int current_minute, bool clock_valid);
/* Internal motion entry point, also used by the software renderer. */
void travel_ui_play_motion(travel_motion_t motion);
