#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "bsp_audio.h"
#include "bsp_pins.h"
#include "travel_content.h"
#include "travel_model.h"
#include "travel_ui.h"
#include "voice_assistant.h"
#include "voice_wifi.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

typedef enum {
    APP_EVENT_BUTTON,
    APP_EVENT_WIFI,
    APP_EVENT_VOICE_STATUS,
    APP_EVENT_EXTERNAL_MESSAGE,
    APP_EVENT_PROGRESS,
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    uint32_t sequence; /* physical button order; zero for non-button events */
    union {
        struct { bsp_btn_t key; bsp_btn_ev_t event; } button;
        voice_wifi_state_t wifi;
        voice_assistant_status_t voice;
        voice_progress_request_t *progress;
        struct { uint8_t kind; uint8_t payload[15]; } external;
    } data;
} app_event_t;

static const char *TAG = "koala";
extern const uint8_t default_pack_start[] asm("_binary_koala_default_start");
extern const uint8_t default_pack_end[] asm("_binary_koala_default_end");
static travel_content_t s_content;
static travel_model_t s_model;
static travel_saved_state_t s_state;
static travel_progress_t s_progress, s_progress_backup;
static SemaphoreHandle_t s_progress_done, s_progress_gate;
static bool s_progress_stopping;
static bool s_progress_cache_incompatible;
static QueueHandle_t s_input;
static QueueHandle_t s_buttons;
static QueueHandle_t s_fallback_clicks;
static uint32_t s_button_sequence; /* button callbacks run on one esp_timer task */
static atomic_uint s_last_physical_input_ms;
static TaskHandle_t s_input_task;
static esp_partition_mmap_handle_t s_pack_map;
static bool s_battery_ready;
static voice_wifi_state_t s_wifi_state = VOICE_WIFI_OFFLINE;
static voice_assistant_status_t s_voice_status = VOICE_ASSISTANT_OFFLINE;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void state_load(void) {
    travel_saved_state_defaults(&s_state);
    nvs_handle_t handle;
    if (nvs_open("passport", NVS_READONLY, &handle) != ESP_OK) return;
    travel_saved_state_t loaded;
    size_t size = sizeof(loaded);
    esp_err_t result = nvs_get_blob(handle, "wa_state3", &loaded, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        size = sizeof(loaded);
        result = nvs_get_blob(handle, "wa_state2", &loaded, &size);
    }
    nvs_close(handle);
    if (result == ESP_OK) travel_saved_state_import(&s_state, &loaded, size);
}

static void state_save(void) {
    nvs_handle_t handle;
    if (nvs_open("passport", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_blob(handle, "wa_state3", &s_state, sizeof(s_state));
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static bool current_perth_time(time_t now, struct tm *local, int *minute) {
    if (!travel_model_clock_valid(now) || !localtime_r(&now, local)) return false;
    if (minute) *minute = local->tm_hour * 60 + local->tm_min;
    return true;
}

static bool apply_automatic_date(time_t now, bool allow_transition) {
    if (s_model.preview) return false;
    struct tm local;
    travel_date_state_t state = TRAVEL_DATE_UNKNOWN;
    int day = 0;
    if (current_perth_time(now, &local, NULL)) {
        day = travel_model_day_for_date(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, &state);
    }
    bool changed = s_model.day != day || s_model.date_state != state;
    s_model.day = (uint8_t)day;
    s_model.date_state = state;
    if (s_model.reminder >= s_content.days[s_model.day].activity_count) s_model.reminder = 0;
    if (allow_transition && state == TRAVEL_DATE_ACTIVE && s_state.last_walk_day != s_model.day) {
        travel_model_start_transition(&s_model, s_model.day, now_ms());
        s_state.last_walk_day = s_model.day;
        state_save();
        return true;
    }
    return changed;
}

static uint8_t preferred_activity(void) {
    const travel_day_t *day = &s_content.days[s_model.day];
    for (size_t i = 0; i < day->activity_count; ++i)
        if (travel_progress_status(&s_progress, day->activities[i].id) == TRAVEL_ACTIVITY_PENDING) return (uint8_t)i;
    return 0; /* completed and skipped activities remain browsable */
}
static void progress_load(void) {
    travel_progress_init(&s_progress, esp_random(), esp_random());
    for (size_t d = 0; d < s_content.day_count; ++d)
        for (size_t a = 0; a < s_content.days[d].activity_count; ++a)
            (void)travel_progress_add(&s_progress, s_content.days[d].activities[a].id);
    nvs_handle_t handle;
    if (nvs_open("passport", NVS_READONLY, &handle) != ESP_OK) return;
    size_t size = sizeof(s_progress_backup);
    esp_err_t result = nvs_get_blob(handle, "wa_progress1", &s_progress_backup, &size);
    nvs_close(handle);
    if (result == ESP_OK && !travel_progress_import(&s_progress, &s_progress_backup, size)) {
        /* Preserve the old blob including its unsent operations until content is reconciled. */
        s_progress_cache_incompatible = true; s_progress.writes_paused = true;
        ESP_LOGW(TAG, "Progress cache catalogue differs; saved operations retained, writes paused");
    } else if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) {
        s_progress_cache_incompatible = true; s_progress.writes_paused = true;
    }
}
static bool progress_save(void) {
    if (s_progress_cache_incompatible) return false;
    nvs_handle_t handle;
    if (nvs_open("passport", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t result = nvs_set_blob(handle, "wa_progress1", &s_progress, sizeof(s_progress));
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle); return result == ESP_OK;
}
/* The worker waits for the state owner to persist an acknowledgement before sending another operation.
 * Only one control worker invokes this callback. No network or LVGL calls run in the state owner. */
static bool on_progress(voice_progress_request_t *request, void *context) {
    (void)context;
    if (!s_input || !s_progress_done || !s_progress_gate) return false;
    app_event_t event = {.type = APP_EVENT_PROGRESS, .data.progress = request};
    xSemaphoreTake(s_progress_gate, portMAX_DELAY);
    bool queued = !s_progress_stopping && xQueueSend(s_input, &event, 0) == pdTRUE;
    xSemaphoreGive(s_progress_gate);
    if (!queued || xSemaphoreTake(s_progress_done, portMAX_DELAY) != pdTRUE) return false;
    return request->accepted;
}
static void process_progress(voice_progress_request_t *request) {
    request->accepted = false;
    if (request->kind == VOICE_PROGRESS_NEXT) {
        request->has_operation = travel_progress_next(&s_progress, &request->operation);
        request->accepted = true;
    } else if (!s_progress_cache_incompatible) {
        s_progress_backup = s_progress;
        bool valid = false;
        if (request->kind == VOICE_PROGRESS_ACK || request->kind == VOICE_PROGRESS_CONFLICT)
            valid = travel_progress_ack(&s_progress, &request->operation, request->snapshot,
                                        request->kind == VOICE_PROGRESS_CONFLICT);
        else if (request->kind == VOICE_PROGRESS_PAUSE) {
            s_progress.writes_paused = true; valid = true;
        } else valid = travel_progress_apply(&s_progress, request->snapshot);
        /* A mismatched snapshot deliberately pauses writes while retaining the queue. */
        if (valid || s_progress.writes_paused != s_progress_backup.writes_paused) {
            if (!memcmp(&s_progress, &s_progress_backup, sizeof(s_progress)) || progress_save())
                request->accepted = valid;
            else s_progress = s_progress_backup;
        } else s_progress = s_progress_backup;
    }
    xSemaphoreGive(s_progress_done);
}

static void on_button(bsp_btn_t key, bsp_btn_ev_t event, void *user) {
    (void)user;
    /* The stop click bypasses the queues, but it must still reset idle time. */
    atomic_store(&s_last_physical_input_ms, now_ms());
    /* A second OK click stops the microphone even if either queue is full. */
    if (key == BSP_BTN_OK && event == BSP_BTN_CLICK && voice_assistant_end_if_active())
        return;
    app_event_t input = {.type = APP_EVENT_BUTTON, .sequence = ++s_button_sequence,
                         .data.button = {key, event}};
    if (s_buttons && xQueueSend(s_buttons, &input, 0) == pdTRUE) return;
    /* Preserve a first click if a burst of other button events filled its queue. */
    if (key == BSP_BTN_OK && event == BSP_BTN_CLICK) {
        if (s_fallback_clicks && xQueueSend(s_fallback_clicks, &input.sequence, 0) == pdTRUE) return;
        if (s_input_task) xTaskNotifyGive(s_input_task);
    }
}

static void on_wifi(voice_wifi_state_t state, void *context) {
    (void)context;
    voice_assistant_set_network(state == VOICE_WIFI_ONLINE);
    app_event_t event = {.type = APP_EVENT_WIFI, .data.wifi = state};
    (void)xQueueSend(s_input, &event, 0);
}

static void on_voice_status(voice_assistant_status_t status, void *context) {
    (void)context;
    app_event_t event = {.type = APP_EVENT_VOICE_STATUS, .data.voice = status};
    (void)xQueueSend(s_input, &event, 0);
}

static bool content_load(void) {
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "travel_cards");
    const void *preferred = NULL;
    bool mapped = partition && esp_partition_mmap(partition, 0, partition->size,
        ESP_PARTITION_MMAP_DATA, &preferred, &s_pack_map) == ESP_OK;
    bool used_builtin = false;
    bool opened = travel_content_open_with_fallback(&s_content,
        mapped ? preferred : NULL, mapped ? partition->size : 0,
        default_pack_start, (size_t)(default_pack_end - default_pack_start), &used_builtin);
    if (mapped && (!opened || used_builtin)) {
        esp_partition_munmap(s_pack_map);
        s_pack_map = 0;
    }
    if (opened) {
        ESP_LOGI(TAG, "Loaded %s card pack, %u days", used_builtin ? "built-in" : "replaceable",
                 (unsigned)s_content.day_count);
        if (mapped && used_builtin) ESP_LOGW(TAG, "Content partition invalid; using built-in Western Australia pack");
    }
    return opened;
}

static void enter_deep_sleep(void) {
    ESP_LOGI(TAG, "Entering deep sleep, wake on GPIO0 button press");
    /* Cancel queued state handshakes before waiting for the voice worker. The gate prevents
     * a new stack-backed request being queued after this drain and avoids a shutdown deadlock. */
    if (s_progress_gate) {
        xSemaphoreTake(s_progress_gate, portMAX_DELAY);
        s_progress_stopping = true;
        app_event_t event;
        while (xQueueReceive(s_input, &event, 0) == pdTRUE) {
            if (event.type == APP_EVENT_PROGRESS) {
                event.data.progress->accepted = false;
                xSemaphoreGive(s_progress_done);
            }
        }
        xSemaphoreGive(s_progress_gate);
    }
    if (!voice_assistant_prepare_sleep()) {
        if (s_progress_gate) {
            xSemaphoreTake(s_progress_gate, portMAX_DELAY);
            s_progress_stopping = false;
            xSemaphoreGive(s_progress_gate);
        }
        ESP_LOGW(TAG, "Voice worker did not stop; deep sleep canceled");
        return;
    }
    if (s_input_task) (void)ulTaskNotifyTake(pdTRUE, 0);
    if (s_buttons) xQueueReset(s_buttons);
    if (s_fallback_clicks) xQueueReset(s_fallback_clicks);
    if (s_battery_ready) bsp_battery_sleep();
    bsp_audio_sleep();
    bsp_audio_prepare_deep_sleep();
    bsp_i2c_prepare_deep_sleep();
    if (bsp_lvgl_lock(1000)) {
        bsp_display_prepare_deep_sleep();
        bsp_lvgl_unlock();
    } else {
        bsp_display_prepare_deep_sleep();
    }
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << GPIO_NUM_0,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << GPIO_NUM_0, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
}

static const char *page_name(travel_page_t page) {
    static const char *names[] = {"home", "schedule", "activities", "feedback", "day_select", "day_transition"};
    return (unsigned)page < sizeof(names) / sizeof(names[0]) ? names[page] : "unknown";
}

static const char *date_state_name(travel_date_state_t state) {
    static const char *names[] = {"unknown", "before", "active", "after", "preview"};
    return (unsigned)state < sizeof(names) / sizeof(names[0]) ? names[state] : "unknown";
}

static voice_state_snapshot_t voice_snapshot(int battery) {
    voice_state_snapshot_t snapshot = {
        .state_revision = s_state.state_revision,
        .progress_revision = s_progress.progress_revision,
        .current_activity_id = s_model.page == TRAVEL_REMINDER || s_model.page == TRAVEL_FEEDBACK ?
            s_content.days[s_model.day].activities[s_model.reminder].id : NULL,
        .current_day = s_model.day,
        .preview_mode = s_model.preview,
        .date_state = date_state_name(s_model.date_state),
        .page = page_name(s_model.page),
        .current_day_id = s_content.days[s_model.day].id,
        .next_reminder = preferred_activity(),
        .battery_percent = battery,
        .device_time = (int64_t)time(NULL),
    };
    for (size_t day = 0; day < TRAVEL_MAX_DAYS; ++day) {
        snapshot.reminder_counts[day] = (uint8_t)s_content.days[day].activity_count;
        for (size_t a = 0; a < s_content.days[day].activity_count; ++a)
            if (travel_progress_status(&s_progress, s_content.days[day].activities[a].id) == TRAVEL_ACTIVITY_COMPLETED)
                snapshot.completion_masks[day] |= (uint8_t)(1u << a);
    }
    return snapshot;
}

static travel_voice_status_t ui_voice_status(void) {
    if (s_wifi_state == VOICE_WIFI_CONFIGURING) return TRAVEL_VOICE_CONFIGURING;
    switch (s_voice_status) {
    case VOICE_ASSISTANT_READY: return TRAVEL_VOICE_READY;
    case VOICE_ASSISTANT_SYNCING: return TRAVEL_VOICE_SYNCING;
    case VOICE_ASSISTANT_LISTENING: return TRAVEL_VOICE_LISTENING;
    case VOICE_ASSISTANT_THINKING: return TRAVEL_VOICE_THINKING;
    case VOICE_ASSISTANT_SPEAKING: return TRAVEL_VOICE_SPEAKING;
    case VOICE_ASSISTANT_ERROR: return TRAVEL_VOICE_ERROR;
    default: return TRAVEL_VOICE_OFFLINE;
    }
}

static bool voice_busy(void) {
    return s_voice_status == VOICE_ASSISTANT_SYNCING || s_voice_status == VOICE_ASSISTANT_LISTENING ||
           s_voice_status == VOICE_ASSISTANT_THINKING || s_voice_status == VOICE_ASSISTANT_SPEAKING;
}

static void save_selection_and_walk(bool day_changed) {
    s_state.preview_mode = s_model.preview;
    s_state.preview_day = s_model.day;
    if (day_changed) s_state.last_walk_day = s_model.day;
    state_save();
}

static void input_task(void *arg) {
    (void)arg;
    int battery = s_battery_ready ? bsp_battery_soc() : -1;
    uint32_t battery_time = now_ms();
    uint32_t last_input = battery_time;
    uint32_t config_started_ms = 0;
    unsigned backlight = 75;
    bool redraw = true;
    time_t last_minute = 0;
    for (;;) {
        app_event_t event;
        app_event_t next_button;
        uint32_t next_fallback;
        bool queued_button = s_buttons && xQueuePeek(s_buttons, &next_button, 0) == pdTRUE;
        bool queued_fallback = s_fallback_clicks &&
                               xQueuePeek(s_fallback_clicks, &next_fallback, 0) == pdTRUE;
        bool has_event = false;
        if (queued_button && (!queued_fallback ||
            (int32_t)(next_button.sequence - next_fallback) < 0)) {
            has_event = xQueueReceive(s_buttons, &event, 0) == pdTRUE;
        } else if (queued_fallback && xQueueReceive(s_fallback_clicks, &next_fallback, 0) == pdTRUE) {
            event = (app_event_t){.type = APP_EVENT_BUTTON, .sequence = next_fallback,
                .data.button = {BSP_BTN_OK, BSP_BTN_CLICK}};
            has_event = true;
        }
        if (!has_event && ulTaskNotifyTake(pdFALSE, 0) != 0) {
            event = (app_event_t){.type = APP_EVENT_BUTTON,
                .data.button = {BSP_BTN_OK, BSP_BTN_CLICK}};
            has_event = true;
        }
        if (!has_event) has_event = xQueueReceive(s_input, &event, pdMS_TO_TICKS(100)) == pdTRUE;
        if (has_event) {
            if (event.type == APP_EVENT_PROGRESS) {
                process_progress(event.data.progress);
                redraw = true;
                continue;
            }
            if (event.type == APP_EVENT_BUTTON) {
                bsp_btn_t key = event.data.button.key;
                bsp_btn_ev_t key_event = event.data.button.event;
                last_input = now_ms();
                if (backlight != 75) { bsp_display_backlight(75); backlight = 75; }
                if (key == BSP_BTN_OK && key_event == BSP_BTN_CLICK) {
                    if (voice_assistant_end_if_active()) { redraw = true; continue; }
                    if (s_model.page == TRAVEL_HOME) {
                        voice_state_snapshot_t snapshot = voice_snapshot(battery);
                        (void)voice_assistant_begin(&snapshot);
                        redraw = true;
                        continue;
                    }
                }
                if (key == BSP_BTN_UP && key_event == BSP_BTN_DOUBLE && s_model.page == TRAVEL_HOME) {
                    voice_wifi_start_configuration();
                    redraw = true;
                    continue;
                }
                if (key_event != BSP_BTN_CLICK && key_event != BSP_BTN_LONG) continue;
                travel_input_t input = key_event == BSP_BTN_LONG ?
                    (key == BSP_BTN_UP ? TRAVEL_UP_LONG : key == BSP_BTN_DOWN ? TRAVEL_DOWN_LONG : TRAVEL_OK_LONG) :
                    (key == BSP_BTN_UP ? TRAVEL_UP : key == BSP_BTN_DOWN ? TRAVEL_DOWN : TRAVEL_OK);
                const travel_day_t *day = &s_content.days[s_model.day];
                if (input == TRAVEL_DOWN && s_model.page != TRAVEL_REMINDER) {
                    uint8_t reminder = preferred_activity();
                    s_model.reminder = reminder == TRAVEL_DAY_NONE ? 0 : reminder;
                }
                travel_action_t action = travel_model_input(&s_model, input,
                    TRAVEL_SCHEDULE_CARD_COUNT, day->activity_count, now_ms());
                if (action == TRAVEL_COMPLETE_REMINDER) {
                    s_progress_backup = s_progress;
                    if (travel_progress_mark(&s_progress, day->activities[s_model.reminder].id, TRAVEL_ACTIVITY_COMPLETED)) {
                        if (!memcmp(&s_progress, &s_progress_backup, sizeof(s_progress)) || progress_save())
                            voice_assistant_refresh_progress();
                        else s_progress = s_progress_backup;
                    }
                } else if (action == TRAVEL_SAVE_SELECTION || action == TRAVEL_DAY_CHANGED) {
                    s_state.state_revision++;
                    save_selection_and_walk(action == TRAVEL_DAY_CHANGED);
                    if (!s_model.preview) redraw |= apply_automatic_date(time(NULL), true);
                } else if (action == TRAVEL_ENTER_DEEP_SLEEP) {
                    enter_deep_sleep();
                }
                redraw = true;
            } else if (event.type == APP_EVENT_WIFI) {
                redraw = true;
            } else if (event.type == APP_EVENT_VOICE_STATUS) {
                redraw = true;
            } else {
                /* Reserved for a future ESP-NOW message source. */
                ESP_LOGD(TAG, "Ignored external event kind %u", event.data.external.kind);
            }
        }

        /* Status notifications may be dropped when the shared queue is full. */
        voice_assistant_status_t voice_status = voice_assistant_get_status();
        if (voice_status != s_voice_status) { s_voice_status = voice_status; redraw = true; }
        voice_wifi_state_t wifi_state = voice_wifi_get_state();
        if (wifi_state != s_wifi_state) {
            s_wifi_state = wifi_state;
            config_started_ms = wifi_state == VOICE_WIFI_CONFIGURING ? now_ms() : 0;
            redraw = true;
        }

        uint32_t physical_input_ms = atomic_load(&s_last_physical_input_ms);
        if (physical_input_ms && (int32_t)(physical_input_ms - last_input) > 0) {
            last_input = physical_input_ms;
            if (backlight != 75) { bsp_display_backlight(75); backlight = 75; }
        }
        uint32_t now = now_ms();
        redraw |= travel_model_tick(&s_model, now);
        /* First-boot captive AP is useful for setup, but an unattended device
         * must be allowed to sleep. A button wakes it and restarts setup. */
        bool configuring = s_wifi_state == VOICE_WIFI_CONFIGURING && config_started_ms &&
                           (uint32_t)(now - config_started_ms) < 300000;
        bool busy = voice_busy() || configuring;
        unsigned next_backlight = travel_backlight_level((uint32_t)(now - last_input), busy);
        if (next_backlight != backlight) { bsp_display_backlight(next_backlight); backlight = next_backlight; }
        if (!busy && travel_model_should_sleep((uint32_t)(now - last_input))) {
            enter_deep_sleep();
            last_input = now_ms(); /* Back off if the voice worker could not stop. */
        }

        time_t current = time(NULL);
        if (current / 60 != last_minute / 60) {
            last_minute = current;
            redraw = true;
            (void)apply_automatic_date(current, true);
        }
        if ((uint32_t)(now - battery_time) >= 30000) {
            int fresh = s_battery_ready ? bsp_battery_soc() : -1;
            redraw |= fresh != battery;
            battery = fresh;
            battery_time = now;
        }
        if (redraw && bsp_lvgl_lock(200)) {
            travel_ui_set_voice_status(ui_voice_status());
            struct tm local;
            int minute = 0;
            bool valid = current_perth_time(current, &local, &minute);
            travel_ui_refresh(&s_content, &s_model, &s_progress, battery, minute, valid);
            bsp_lvgl_unlock();
            redraw = false;
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Boot cause: %d", (int)esp_sleep_get_wakeup_cause());
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
    state_load();
    setenv("TZ", "AWST-8", 1);
    tzset();

    ESP_LOGI(TAG, "Koala fixed Western Australia itinerary, offline-first");
    bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }
    s_battery_ready = bsp_battery_init() == ESP_OK;
    if (!bsp_lvgl_lock(2000)) { ESP_LOGE(TAG, "Initial UI lock unavailable"); return; }
    if (!content_load()) { bsp_lvgl_unlock(); ESP_LOGE(TAG, "Default card pack could not be loaded"); return; }

    progress_load();
    travel_model_restore(&s_model, &s_state);
    if (s_model.preview) {
        if (s_state.last_walk_day != s_model.day) {
            travel_model_start_transition(&s_model, s_model.day, now_ms());
            s_state.last_walk_day = s_model.day;
            state_save();
        }
    } else {
        apply_automatic_date(time(NULL), true);
    }
    travel_ui_create(&s_content);
    struct tm local;
    int minute = 0;
    bool clock_valid = current_perth_time(time(NULL), &local, &minute);
    travel_ui_refresh(&s_content, &s_model, &s_progress, -1, minute, clock_valid);
    lv_mem_monitor_t ui_heap = {0};
    lv_mem_monitor(&ui_heap);
    ESP_LOGI(TAG, "LVGL pool: used=%u free=%u largest=%u",
        (unsigned)(ui_heap.total_size - ui_heap.free_size),
        (unsigned)ui_heap.free_size, (unsigned)ui_heap.free_biggest_size);
    bsp_lvgl_unlock();
    bsp_display_backlight(75);

    s_progress_done = xSemaphoreCreateBinary();
    s_progress_gate = xSemaphoreCreateMutex();
    s_input = xQueueCreate(12, sizeof(app_event_t));
    s_buttons = xQueueCreate(16, sizeof(app_event_t));
    s_fallback_clicks = xQueueCreate(16, sizeof(uint32_t));
    if (!s_input || !s_buttons || !s_fallback_clicks || !s_progress_done || !s_progress_gate ||
        xTaskCreate(input_task, "koala_input", 4096, NULL, 5, &s_input_task) != pdPASS) {
        ESP_LOGE(TAG, "Input task allocation failed");
        if (s_input) { vQueueDelete(s_input); s_input = NULL; }
        if (s_buttons) { vQueueDelete(s_buttons); s_buttons = NULL; }
        if (s_fallback_clicks) { vQueueDelete(s_fallback_clicks); s_fallback_clicks = NULL; }
        return;
    }
    if (bsp_button_init(on_button, NULL) != ESP_OK) ESP_LOGE(TAG, "Buttons unavailable; homepage remains visible");
    esp_err_t wifi_result = voice_wifi_init(on_wifi, NULL);
    if (wifi_result != ESP_OK) ESP_LOGE(TAG, "Wi-Fi unavailable: %s", esp_err_to_name(wifi_result));
    voice_assistant_set_progress_callback(on_progress, NULL);
    esp_err_t voice_result = voice_assistant_init(on_voice_status, NULL);
    if (voice_result != ESP_OK) {
        ESP_LOGE(TAG, "Voice assistant unavailable: %s", esp_err_to_name(voice_result));
    } else if (wifi_result == ESP_OK && voice_wifi_is_connected()) {
        /* A very fast station connection may precede voice initialization. */
        voice_assistant_set_network(true);
    }
    ESP_LOGI(TAG, "UI ready: free heap=%u, largest block=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Ready: UP=schedule, DOWN=activities, OK click=talk/send, hold UP=date, double UP=Wi-Fi, hold DOWN=sleep");
}
