#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOICE_WIFI_OFFLINE,
    VOICE_WIFI_CONNECTING,
    VOICE_WIFI_ONLINE,
    VOICE_WIFI_CONFIGURING,
} voice_wifi_state_t;

typedef void (*voice_wifi_callback_t)(voice_wifi_state_t state, void *context);

esp_err_t voice_wifi_init(voice_wifi_callback_t callback, void *context);
bool voice_wifi_is_connected(void);
voice_wifi_state_t voice_wifi_get_state(void);
void voice_wifi_start_configuration(void);
size_t voice_wifi_device_id(char *destination, size_t capacity);

#ifdef __cplusplus
}
#endif
