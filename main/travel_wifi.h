#pragma once
#include "esp_err.h"
#include "travel_ui.h"
typedef enum { TRAVEL_WIFI_PAIR, TRAVEL_WIFI_END_PAIR, TRAVEL_WIFI_FORGET } travel_wifi_command_t;
esp_err_t travel_wifi_init(void);
bool travel_wifi_command(travel_wifi_command_t command);
travel_net_status_t travel_wifi_status(void);
