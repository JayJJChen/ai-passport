#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { TRAVEL_HOME, TRAVEL_GREETING, TRAVEL_TASK, TRAVEL_REPLY,
               TRAVEL_SETTINGS, TRAVEL_PROVISION, TRAVEL_PLACES,
               TRAVEL_FORGET_CONFIRM } travel_page_t;
typedef enum { TRAVEL_UP, TRAVEL_DOWN, TRAVEL_OK, TRAVEL_OK_LONG } travel_input_t;
typedef enum { TRAVEL_NO_ACTION, TRAVEL_START_PROVISION, TRAVEL_STOP_PROVISION,
               TRAVEL_FORGET_WIFI, TRAVEL_CHANGE_PLACE } travel_action_t;
typedef struct {
    travel_page_t page;
    size_t place, greeting, task, selection;
    uint32_t reply_since;
} travel_model_t;

void travel_model_init(travel_model_t *model);
travel_action_t travel_model_input(travel_model_t *model, travel_input_t input,
                                  size_t places, size_t greetings, size_t tasks,
                                  uint32_t now);
bool travel_model_tick(travel_model_t *model, uint32_t now);
/* Hardware keys divide the full display height into three equal bands. */
int travel_key_center(unsigned key, int height);
/* Backlight only; the button service remains awake. Pairing is bounded separately. */
unsigned travel_backlight_level(uint32_t idle_ms, bool pairing);

/* Battery gauge layout geometry (outer dimensions, border, and inner gap) */
#define TRAVEL_BATTERY_GAUGE_W       29
#define TRAVEL_BATTERY_GAUGE_H       12
#define TRAVEL_BATTERY_BORDER_W      2
#define TRAVEL_BATTERY_INNER_GAP     1
#define TRAVEL_BATTERY_FILL_MAX_W    (TRAVEL_BATTERY_GAUGE_W - 2 * (TRAVEL_BATTERY_BORDER_W + TRAVEL_BATTERY_INNER_GAP))
#define TRAVEL_BATTERY_FILL_H        (TRAVEL_BATTERY_GAUGE_H - 2 * (TRAVEL_BATTERY_BORDER_W + TRAVEL_BATTERY_INNER_GAP))

/* Battery gauge inner fill width in pixels, scaled to 0..TRAVEL_BATTERY_FILL_MAX_W px. */
int travel_battery_fill_width(int soc);
