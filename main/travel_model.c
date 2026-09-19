#define _POSIX_C_SOURCE 200809L
#include "travel_model.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
void travel_model_init(travel_model_t *m) { memset(m, 0, sizeof(*m)); }
void travel_model_stamp(travel_model_t *m, travel_stamp_record_t *rec, uint32_t now_epoch) {
    if (!m || !rec) return;
    if (m->task < TRAVEL_MAX_TASKS) {
        rec->mask |= (1u << m->task);
        rec->timestamps[m->task] = now_epoch;
    }
}
bool travel_model_is_stamped(const travel_stamp_record_t *rec, size_t task_idx) {
    if (!rec || task_idx >= TRAVEL_MAX_TASKS) return false;
    return (rec->mask & (1u << task_idx)) != 0;
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
void travel_model_format_time(time_t t, char *buf, size_t len) {
    if (!buf || len == 0) return;
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(buf, len, "%02d/%02d %02d:%02d", tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
}
int travel_battery_fill_width(int soc) {
    if (soc <= 0) return 0;
    if (soc >= 100) return TRAVEL_BATTERY_FILL_MAX_W;
    return (soc * TRAVEL_BATTERY_FILL_MAX_W) / 100;
}
static size_t next(size_t index, size_t count) { return count ? (index + 1) % count : 0; }

travel_action_t travel_model_input(travel_model_t *m, travel_input_t in,
                                  size_t places, size_t greetings, size_t tasks,
                                  uint32_t now) {
    (void)places;
    if (in == TRAVEL_DOWN_LONG) {
        return TRAVEL_ENTER_DEEP_SLEEP;
    }
    if (in == TRAVEL_UP_LONG) {
        if (m->page == TRAVEL_MAINTENANCE) return TRAVEL_NO_ACTION;
        m->page = TRAVEL_MAINTENANCE;
        m->selection = 0;
        return TRAVEL_START_SOFTAP;
    }
    if (in == TRAVEL_OK_LONG) {
        if (m->page == TRAVEL_MAINTENANCE) {
            m->page = TRAVEL_HOME;
            return TRAVEL_STOP_SOFTAP;
        }
        if (m->page == TRAVEL_PASSPORT) {
            m->page = TRAVEL_HOME;
            return TRAVEL_NO_ACTION;
        }
        m->page = TRAVEL_PASSPORT;
        m->selection = 0;
        return TRAVEL_NO_ACTION;
    }
    switch (m->page) {
    case TRAVEL_PASSPORT:
        if (in == TRAVEL_UP) {
            if (tasks > 0) m->selection = (m->selection + tasks - 1) % tasks;
        } else if (in == TRAVEL_DOWN) {
            if (tasks > 0) m->selection = (m->selection + 1) % tasks;
        } else if (in == TRAVEL_OK) {
            m->page = TRAVEL_HOME;
        }
        break;
    case TRAVEL_MAINTENANCE:
        if (in == TRAVEL_OK) {
            m->page = TRAVEL_HOME;
            return TRAVEL_STOP_SOFTAP;
        }
        break;
    case TRAVEL_STAMP_ANIM:
    case TRAVEL_REPLY:
        if (in == TRAVEL_OK || in == TRAVEL_UP || in == TRAVEL_DOWN) {
            m->page = TRAVEL_HOME;
        }
        break;
    default:
        if (in == TRAVEL_UP) {
            if (m->page == TRAVEL_GREETING) m->greeting = next(m->greeting, greetings);
            m->page = TRAVEL_GREETING;
        } else if (in == TRAVEL_DOWN) {
            if (m->page == TRAVEL_TASK) m->task = next(m->task, tasks);
            m->page = TRAVEL_TASK;
        } else if (in == TRAVEL_OK) {
            m->page = TRAVEL_STAMP_ANIM;
            m->reply_since = now;
            return TRAVEL_STAMP_CURRENT;
        }
        break;
    }
    return TRAVEL_NO_ACTION;
}
bool travel_model_tick(travel_model_t *m, uint32_t now) {
    if ((m->page != TRAVEL_REPLY && m->page != TRAVEL_STAMP_ANIM) ||
        (uint32_t)(now - m->reply_since) < 3000) {
        return false;
    }
    m->page = TRAVEL_HOME;
    return true;
}
