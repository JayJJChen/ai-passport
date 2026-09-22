#pragma once
#include "travel_animation.h"
#include "travel_content.h"
#include "travel_model.h"
typedef enum {
    TRAVEL_VOICE_OFFLINE,
    TRAVEL_VOICE_CONFIGURING,
    TRAVEL_VOICE_READY,
    TRAVEL_VOICE_SYNCING,
    TRAVEL_VOICE_LISTENING,
    TRAVEL_VOICE_THINKING,
    TRAVEL_VOICE_SPEAKING,
    TRAVEL_VOICE_ERROR,
} travel_voice_status_t;
void travel_ui_create(const travel_content_t *content);
void travel_ui_set_voice_status(travel_voice_status_t status);
void travel_ui_refresh(const travel_content_t *content, const travel_model_t *model,
                       const travel_progress_t *progress, int battery,
                       int current_minute, bool clock_valid);
/* Internal motion entry point, also used by the software renderer. */
void travel_ui_play_motion(travel_motion_t motion);
