#include "travel_model.h"
#include <string.h>

void travel_model_init(travel_model_t *m) { memset(m, 0, sizeof(*m)); }
int travel_key_center(unsigned key, int height) {
    return key < 3 && height > 0 ? (int)(((2 * key + 1) * (unsigned)height + 3) / 6) : -1;
}
unsigned travel_backlight_level(uint32_t idle_ms, bool pairing) {
    return pairing ? 75 : idle_ms >= 60000 ? 10 : idle_ms >= 30000 ? 20 : 75;
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
    if (in == TRAVEL_OK_LONG) {
        travel_action_t action = m->page == TRAVEL_PROVISION
                               ? TRAVEL_STOP_PROVISION : TRAVEL_NO_ACTION;
        m->page = m->page >= TRAVEL_SETTINGS ? TRAVEL_HOME : TRAVEL_SETTINGS;
        m->selection = 0;
        return action;
    }
    switch (m->page) {
    case TRAVEL_SETTINGS:
        if (in == TRAVEL_UP) m->selection = (m->selection + 3) % 4;
        else if (in == TRAVEL_DOWN) m->selection = (m->selection + 1) % 4;
        else if (in == TRAVEL_OK) {
            if (m->selection == 0) { m->page = TRAVEL_PROVISION; return TRAVEL_START_PROVISION; }
            if (m->selection == 1) m->page = TRAVEL_FORGET_CONFIRM;
            else if (m->selection == 2) { m->page = TRAVEL_PLACES; m->selection = m->place; }
            else if (m->selection == 3) m->page = TRAVEL_HOME;
        }
        break;
    case TRAVEL_FORGET_CONFIRM:
        m->page = TRAVEL_SETTINGS; m->selection = 1;
        if (in == TRAVEL_OK) return TRAVEL_FORGET_WIFI;
        break;
    case TRAVEL_PROVISION:
        if (in == TRAVEL_OK) { m->page = TRAVEL_SETTINGS; m->selection = 0; return TRAVEL_STOP_PROVISION; }
        break;
    case TRAVEL_PLACES:
        if (in == TRAVEL_UP && places) m->selection = (m->selection + places - 1) % places;
        else if (in == TRAVEL_DOWN) m->selection = next(m->selection, places);
        else if (in == TRAVEL_OK && places) {
            m->place = m->selection; m->greeting = m->task = 0; m->page = TRAVEL_HOME;
            return TRAVEL_CHANGE_PLACE;
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
            if (m->page == TRAVEL_REPLY) m->page = TRAVEL_HOME;
            else { m->page = TRAVEL_REPLY; m->reply_since = now; }
        }
        break;
    }
    return TRAVEL_NO_ACTION;
}
bool travel_model_tick(travel_model_t *m, uint32_t now) {
    if (m->page != TRAVEL_REPLY || (uint32_t)(now - m->reply_since) < 3000) return false;
    m->page = TRAVEL_HOME;
    return true;
}
