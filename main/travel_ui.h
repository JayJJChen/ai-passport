#pragma once
#include "travel_content.h"
#include "travel_model.h"
typedef enum { TRAVEL_NET_OFFLINE, TRAVEL_NET_CONNECTING, TRAVEL_NET_CONNECTED,
               TRAVEL_NET_PAIRING, TRAVEL_NET_PHONE, TRAVEL_NET_FAILED,
               TRAVEL_NET_TIMEOUT } travel_net_state_t;
typedef struct { travel_net_state_t state; bool provisioning, saved; } travel_net_status_t;
void travel_ui_create(const travel_content_t *content);
void travel_ui_refresh(const travel_content_t *content, const travel_model_t *model,
                       travel_net_status_t network, int battery);
