#pragma once
#include "esp_err.h"
#include "travel_ui.h"
#include "travel_model.h"

typedef void (*travel_wifi_sync_cb_t)(time_t epoch, const travel_custom_schedule_t *schedule);

esp_err_t travel_wifi_init(travel_wifi_sync_cb_t on_sync);
esp_err_t travel_wifi_start_softap(void);
esp_err_t travel_wifi_stop_softap(void);
bool travel_wifi_is_softap_active(void);
travel_net_status_t travel_wifi_status(void);
void travel_wifi_set_current_trip(const travel_custom_schedule_t *schedule);
