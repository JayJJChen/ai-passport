#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "voice_state_payload.h"
#include "travel_progress.h"

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

typedef enum { VOICE_PROGRESS_NEXT, VOICE_PROGRESS_SNAPSHOT, VOICE_PROGRESS_ACK,
               VOICE_PROGRESS_CONFLICT, VOICE_PROGRESS_PAUSE } voice_progress_request_kind_t;
typedef struct {
    voice_progress_request_kind_t kind;
    travel_progress_operation_t operation;
    const travel_progress_snapshot_t *snapshot;
    bool has_operation;
    bool accepted;
} voice_progress_request_t;
/* Called only by the voice control worker; callback queues work to the state owner. */
typedef bool (*voice_progress_callback_t)(voice_progress_request_t *request, void *context);
void voice_assistant_set_progress_callback(voice_progress_callback_t callback, void *context);
void voice_assistant_refresh_progress(void);

esp_err_t voice_assistant_init(voice_assistant_status_callback_t callback, void *context);
void voice_assistant_set_network(bool online);
bool voice_assistant_begin(const voice_state_snapshot_t *snapshot);
void voice_assistant_end(void);
void voice_assistant_prepare_sleep(void);
voice_assistant_status_t voice_assistant_get_status(void);

#ifdef __cplusplus
}
#endif
