#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "voice_state_payload.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOICE_ASSISTANT_OFFLINE,
    VOICE_ASSISTANT_READY,
    VOICE_ASSISTANT_SYNCING,
    VOICE_ASSISTANT_LISTENING,
    VOICE_ASSISTANT_THINKING,
    VOICE_ASSISTANT_SPEAKING,
    VOICE_ASSISTANT_ERROR,
} voice_assistant_status_t;

typedef void (*voice_assistant_status_callback_t)(voice_assistant_status_t status,
                                                   void *context);

esp_err_t voice_assistant_init(voice_assistant_status_callback_t callback, void *context);
void voice_assistant_set_network(bool online);
bool voice_assistant_begin(const voice_state_snapshot_t *snapshot);
void voice_assistant_end(void);
void voice_assistant_prepare_sleep(void);
voice_assistant_status_t voice_assistant_get_status(void);

#ifdef __cplusplus
}
#endif
