#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TRAVEL_HOME,
    TRAVEL_SCHEDULE,
    TRAVEL_REMINDER,
    TRAVEL_FEEDBACK,
    TRAVEL_DAY_SELECT,
    TRAVEL_DAY_TRANSITION
} travel_page_t;

typedef enum {
    TRAVEL_UP,
    TRAVEL_DOWN,
    TRAVEL_OK,
    TRAVEL_OK_LONG,
    TRAVEL_UP_LONG,
    TRAVEL_DOWN_LONG
} travel_input_t;

typedef enum {
    TRAVEL_NO_ACTION,
    TRAVEL_COMPLETE_REMINDER,
    TRAVEL_SAVE_SELECTION,
    TRAVEL_DAY_CHANGED,
    TRAVEL_ENTER_DEEP_SLEEP,
} travel_action_t;

#define TRAVEL_MAX_DAYS 11
#define TRAVEL_MAX_REMINDERS 4 /* legacy cache layout only */
#define TRAVEL_MAX_ACTIVITIES 3
#define TRAVEL_SCHEDULE_CARD_COUNT 3
#define TRAVEL_FEEDBACK_MS 3000U
#define TRAVEL_TRANSITION_MS 3350U
#define TRAVEL_DAY_NONE 0xffU
#define TRAVEL_SAVED_STATE_VERSION 3U

typedef struct {
    uint8_t completed[TRAVEL_MAX_DAYS];
} travel_completion_t;

typedef struct {
    uint32_t version;
    travel_completion_t completion;
    uint8_t preview_mode;
    uint8_t preview_day;
    uint8_t last_walk_day;
    uint8_t reserved;
    uint32_t state_revision;
} travel_saved_state_t;

typedef enum {
    TRAVEL_DATE_UNKNOWN,
    TRAVEL_DATE_BEFORE,
    TRAVEL_DATE_ACTIVE,
    TRAVEL_DATE_AFTER,
    TRAVEL_DATE_PREVIEW
} travel_date_state_t;

typedef struct {
    travel_page_t page;
    uint8_t day;
    uint8_t schedule;
    uint8_t reminder;
    uint8_t selection;
    bool preview;
    travel_date_state_t date_state;
    uint32_t page_since;
} travel_model_t;
void travel_model_init(travel_model_t *model);
void travel_saved_state_defaults(travel_saved_state_t *state);
bool travel_saved_state_import(travel_saved_state_t *state, const void *data, size_t size);
void travel_model_restore(travel_model_t *model, const travel_saved_state_t *state);
travel_action_t travel_model_input(travel_model_t *model, travel_input_t input,
                                  size_t schedule_count, size_t reminder_count,
                                  uint32_t now);
bool travel_model_tick(travel_model_t *model, uint32_t now);
void travel_model_start_transition(travel_model_t *model, uint8_t day, uint32_t now);
bool travel_model_complete(const travel_model_t *model, travel_completion_t *completion);
bool travel_model_is_complete(const travel_completion_t *completion, size_t day, size_t reminder);
uint8_t travel_model_next_reminder(const int16_t *minute_of_day, size_t reminder_count,
                                   uint8_t completed_mask, bool clock_valid,
                                   bool preview, int current_minute);
int travel_model_day_for_date(int year, int month, int day, travel_date_state_t *state);
int travel_model_date_key_for_day(size_t day);
void travel_model_format_day(size_t day, char *buf, size_t len);
bool travel_model_clock_valid(time_t t);
/* Only accept plausible Unix seconds from the authenticated trip service. */
bool travel_model_server_time_valid(int64_t seconds);
/* Hardware keys divide the full display height into three equal bands. */
int travel_key_center(unsigned key, int height);
/* Backlight only; the button service remains awake. Pairing is bounded separately. */
unsigned travel_backlight_level(uint32_t idle_ms, bool pairing);
/* Automatic sleep predicate: deep sleep after 2 minutes of inactivity. */
bool travel_model_should_sleep(uint32_t idle_ms);
/* Format a valid minute of day into 24-hour "HH:MM"; otherwise use "--:--". */
void travel_model_format_time(int minute_of_day, bool clock_valid, char *buf, size_t len);

/* Battery gauge layout geometry (outer dimensions, border, and inner gap) */
#define TRAVEL_BATTERY_GAUGE_W       29
#define TRAVEL_BATTERY_GAUGE_H       12
#define TRAVEL_BATTERY_BORDER_W      2
#define TRAVEL_BATTERY_INNER_GAP     1
#define TRAVEL_BATTERY_FILL_MAX_W    (TRAVEL_BATTERY_GAUGE_W - 2 * (TRAVEL_BATTERY_BORDER_W + TRAVEL_BATTERY_INNER_GAP))
#define TRAVEL_BATTERY_FILL_H        (TRAVEL_BATTERY_GAUGE_H - 2 * (TRAVEL_BATTERY_BORDER_W + TRAVEL_BATTERY_INNER_GAP))

/* Battery gauge inner fill width in pixels, scaled to 0..TRAVEL_BATTERY_FILL_MAX_W px. */
int travel_battery_fill_width(int soc);

#ifdef __cplusplus
}
#endif
