#define _POSIX_C_SOURCE 200809L
#include "travel_model.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const int k_trip_dates[TRAVEL_MAX_DAYS] = {
    20261002, 20261003, 20261004, 20261005, 20261006, 20261007,
    20261008, 20261009, 20261010, 20261011, 20261012,
};

void travel_model_init(travel_model_t *m) {
    memset(m, 0, sizeof(*m));
    m->date_state = TRAVEL_DATE_UNKNOWN;
}

void travel_saved_state_defaults(travel_saved_state_t *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->version = TRAVEL_SAVED_STATE_VERSION;
    state->last_walk_day = TRAVEL_DAY_NONE;
}

bool travel_saved_state_import(travel_saved_state_t *state, const void *data, size_t size) {
    if (!state) return false;
    travel_saved_state_defaults(state);
    if (!data) return false;
    typedef struct {
        uint32_t version;
        travel_completion_t completion;
        uint8_t preview_mode;
        uint8_t preview_day;
        uint8_t last_walk_day;
        uint8_t reserved;
    } travel_saved_state_v1_t;
    if (size == sizeof(travel_saved_state_v1_t)) {
        travel_saved_state_v1_t old;
        memcpy(&old, data, sizeof(old));
        if (old.version != 1U || old.preview_mode > 1 || old.preview_day >= TRAVEL_MAX_DAYS ||
            (old.last_walk_day >= TRAVEL_MAX_DAYS && old.last_walk_day != TRAVEL_DAY_NONE)) return false;
        for (size_t day = 0; day < TRAVEL_MAX_DAYS; ++day)
            if (old.completion.completed[day] & ~((1u << TRAVEL_MAX_REMINDERS) - 1u)) return false;
        /* Legacy habit bits are deliberately not activity progress. */
        state->preview_mode = old.preview_mode;
        state->preview_day = old.preview_day;
        state->last_walk_day = old.last_walk_day;
        return true;
    }
    if (size != sizeof(*state)) return false;
    travel_saved_state_t loaded;
    memcpy(&loaded, data, sizeof(loaded));
    if ((loaded.version != TRAVEL_SAVED_STATE_VERSION && loaded.version != 2U) || loaded.preview_mode > 1 ||
        loaded.preview_day >= TRAVEL_MAX_DAYS ||
        (loaded.last_walk_day >= TRAVEL_MAX_DAYS && loaded.last_walk_day != TRAVEL_DAY_NONE)) return false;
    for (size_t day = 0; day < TRAVEL_MAX_DAYS; ++day)
        if (loaded.completion.completed[day] & ~((1u << TRAVEL_MAX_REMINDERS) - 1u)) return false;
    memset(&loaded.completion, 0, sizeof(loaded.completion));
    loaded.version = TRAVEL_SAVED_STATE_VERSION;
    *state = loaded;
    return true;
}

void travel_model_restore(travel_model_t *model, const travel_saved_state_t *state) {
    travel_model_init(model);
    if (!state || !state->preview_mode) return;
    model->preview = true;
    model->day = state->preview_day;
    model->date_state = TRAVEL_DATE_PREVIEW;
}

bool travel_model_complete(const travel_model_t *m, travel_completion_t *completion) {
    if (!m || !completion || m->day >= TRAVEL_MAX_DAYS || m->reminder >= TRAVEL_MAX_REMINDERS) return false;
    uint8_t bit = (uint8_t)(1u << m->reminder);
    bool changed = (completion->completed[m->day] & bit) == 0;
    completion->completed[m->day] |= bit;
    return changed;
}

bool travel_model_is_complete(const travel_completion_t *completion, size_t day, size_t reminder) {
    if (!completion || day >= TRAVEL_MAX_DAYS || reminder >= TRAVEL_MAX_REMINDERS) return false;
    return (completion->completed[day] & (1u << reminder)) != 0;
}
int travel_key_center(unsigned key, int height) {
    return key < 3 && height > 0 ? (int)(((2 * key + 1) * (unsigned)height + 3) / 6) : -1;
}
unsigned travel_backlight_level(uint32_t idle_ms, bool pairing) {
    return pairing ? 75 : idle_ms >= 60000 ? 10 : idle_ms >= 30000 ? 20 : 75;
}
bool travel_model_should_sleep(uint32_t idle_ms) {
    return idle_ms >= 120000;
}
void travel_model_format_time(int minute_of_day, bool clock_valid, char *buf, size_t len) {
    if (!buf || len == 0) return;
    if (!clock_valid || minute_of_day < 0 || minute_of_day >= 24 * 60) {
        snprintf(buf, len, "--:--");
        return;
    }
    snprintf(buf, len, "%02d:%02d", minute_of_day / 60, minute_of_day % 60);
}
int travel_battery_fill_width(int soc) {
    if (soc <= 0) return 0;
    if (soc >= 100) return TRAVEL_BATTERY_FILL_MAX_W;
    return (soc * TRAVEL_BATTERY_FILL_MAX_W) / 100;
}
static size_t next(size_t index, size_t count) { return count ? (index + 1) % count : 0; }

void travel_model_start_transition(travel_model_t *m, uint8_t day, uint32_t now) {
    if (!m) return;
    m->day = day < TRAVEL_MAX_DAYS ? day : TRAVEL_MAX_DAYS - 1;
    m->schedule = 0;
    m->reminder = 0;
    m->page = TRAVEL_DAY_TRANSITION;
    m->page_since = now;
}

travel_action_t travel_model_input(travel_model_t *m, travel_input_t in,
                                  size_t schedule_count, size_t reminder_count,
                                  uint32_t now) {
    if (in == TRAVEL_DOWN_LONG) {
        return TRAVEL_ENTER_DEEP_SLEEP;
    }
    if (in == TRAVEL_OK_LONG) {
        return TRAVEL_NO_ACTION;
    }
    if (in == TRAVEL_UP_LONG) {
        if (m->page == TRAVEL_DAY_TRANSITION) return TRAVEL_NO_ACTION;
        m->page = TRAVEL_DAY_SELECT;
        m->selection = m->preview ? (uint8_t)(m->day + 1) : 0;
        return TRAVEL_NO_ACTION;
    }
    if (m->page == TRAVEL_DAY_TRANSITION) return TRAVEL_NO_ACTION;

    switch (m->page) {
    case TRAVEL_DAY_SELECT:
        if (in == TRAVEL_UP) {
            m->selection = (uint8_t)((m->selection + TRAVEL_MAX_DAYS) % (TRAVEL_MAX_DAYS + 1));
        } else if (in == TRAVEL_DOWN) {
            m->selection = (uint8_t)((m->selection + 1) % (TRAVEL_MAX_DAYS + 1));
        } else if (in == TRAVEL_OK) {
            bool old_preview = m->preview;
            uint8_t old_day = m->day;
            m->preview = m->selection != 0;
            if (m->preview) {
                m->day = (uint8_t)(m->selection - 1);
                m->date_state = TRAVEL_DATE_PREVIEW;
            }
            m->page = TRAVEL_HOME;
            (void)old_preview;
            if (m->preview && old_day != m->day) {
                travel_model_start_transition(m, m->day, now);
                return TRAVEL_DAY_CHANGED;
            }
            return TRAVEL_SAVE_SELECTION;
        }
        break;
    case TRAVEL_FEEDBACK:
        /* Keep the acknowledgement and nod visible for the full three seconds. */
        break;
    default:
        if (in == TRAVEL_UP) {
            if (m->page == TRAVEL_SCHEDULE) {
                if ((size_t)m->schedule + 1 >= schedule_count) m->page = TRAVEL_HOME;
                else m->schedule++;
            } else {
                m->schedule = 0;
                m->page = TRAVEL_SCHEDULE;
            }
        } else if (in == TRAVEL_DOWN) {
            if (m->page == TRAVEL_REMINDER) m->reminder = (uint8_t)next(m->reminder, reminder_count);
            m->page = TRAVEL_REMINDER;
        } else if (in == TRAVEL_OK) {
            if (m->page == TRAVEL_REMINDER && reminder_count > 0) {
                m->page = TRAVEL_FEEDBACK;
                m->page_since = now;
                return TRAVEL_COMPLETE_REMINDER;
            }
            m->page = TRAVEL_HOME;
        }
        break;
    }
    return TRAVEL_NO_ACTION;
}
bool travel_model_tick(travel_model_t *m, uint32_t now) {
    uint32_t duration = m->page == TRAVEL_FEEDBACK ? TRAVEL_FEEDBACK_MS :
                        m->page == TRAVEL_DAY_TRANSITION ? TRAVEL_TRANSITION_MS : 0;
    if (!duration || (uint32_t)(now - m->page_since) < duration) return false;
    m->page = TRAVEL_HOME;
    return true;
}

uint8_t travel_model_next_reminder(const int16_t *minutes, size_t count,
                                   uint8_t completed_mask, bool clock_valid,
                                   bool preview, int current_minute) {
    if (!minutes || count == 0) return TRAVEL_DAY_NONE;
    if (count > TRAVEL_MAX_REMINDERS) count = TRAVEL_MAX_REMINDERS;
    if (preview || !clock_valid) {
        for (size_t i = 0; i < count; ++i) if (!(completed_mask & (1u << i))) return (uint8_t)i;
        return TRAVEL_DAY_NONE;
    }
    for (size_t i = 0; i < count; ++i)
        if (!(completed_mask & (1u << i)) && minutes[i] >= 0 && minutes[i] <= current_minute) return (uint8_t)i;
    for (size_t i = 0; i < count; ++i)
        if (!(completed_mask & (1u << i)) && minutes[i] < 0) return (uint8_t)i;
    for (size_t i = 0; i < count; ++i)
        if (!(completed_mask & (1u << i))) return (uint8_t)i;
    return TRAVEL_DAY_NONE;
}

int travel_model_day_for_date(int year, int month, int day, travel_date_state_t *state) {
    int key = year * 10000 + month * 100 + day;
    if (key < k_trip_dates[0]) {
        if (state) *state = TRAVEL_DATE_BEFORE;
        return 0;
    }
    if (key > k_trip_dates[TRAVEL_MAX_DAYS - 1]) {
        if (state) *state = TRAVEL_DATE_AFTER;
        return TRAVEL_MAX_DAYS - 1;
    }
    for (int i = 0; i < TRAVEL_MAX_DAYS; ++i) {
        if (key == k_trip_dates[i]) {
            if (state) *state = TRAVEL_DATE_ACTIVE;
            return i;
        }
    }
    if (state) *state = TRAVEL_DATE_UNKNOWN;
    return 0;
}

int travel_model_date_key_for_day(size_t day) {
    return day < TRAVEL_MAX_DAYS ? k_trip_dates[day] : 0;
}

void travel_model_format_day(size_t day, char *buf, size_t len) {
    if (!buf || !len) return;
    int key = travel_model_date_key_for_day(day);
    snprintf(buf, len, "%02d/%02d", (key / 100) % 100, key % 100);
}

bool travel_model_clock_valid(time_t t) {
    struct tm tm;
    if (t <= 0 || !localtime_r(&t, &tm)) return false;
    return tm.tm_year + 1900 >= 2025;
}
