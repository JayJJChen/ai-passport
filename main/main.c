#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "bsp_audio.h"
#include "bsp_pins.h"
#include "travel_content.h"
#include "travel_model.h"
#include "travel_ui.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum {
    APP_EVENT_BUTTON,
    APP_EVENT_EXTERNAL_MESSAGE,
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    union {
        struct { bsp_btn_t key; bsp_btn_ev_t event; } button;
        struct { uint8_t kind; uint8_t payload[15]; } external;
    } data;
} app_event_t;

static const char *TAG = "koala";
extern const uint8_t default_pack_start[] asm("_binary_koala_default_start");
extern const uint8_t default_pack_end[] asm("_binary_koala_default_end");
static travel_content_t s_content;
static travel_model_t s_model;
static travel_saved_state_t s_state;
static QueueHandle_t s_input;
static esp_partition_mmap_handle_t s_pack_map;
static bool s_battery_ready;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void state_load(void) {
    travel_saved_state_defaults(&s_state);
    nvs_handle_t handle;
    if (nvs_open("passport", NVS_READONLY, &handle) != ESP_OK) return;
    travel_saved_state_t loaded;
    size_t size = sizeof(loaded);
    esp_err_t result = nvs_get_blob(handle, "wa_state2", &loaded, &size);
    nvs_close(handle);
    if (result == ESP_OK) travel_saved_state_import(&s_state, &loaded, size);
}

static void state_save(void) {
    nvs_handle_t handle;
    if (nvs_open("passport", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_blob(handle, "wa_state2", &s_state, sizeof(s_state));
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
    if (allow_transition && state == TRAVEL_DATE_ACTIVE && s_state.last_walk_day != s_model.day) {
        travel_model_start_transition(&s_model, s_model.day, now_ms());
        s_state.last_walk_day = s_model.day;
        state_save();
        return true;
    }
    return changed;
}

static uint8_t preferred_reminder(time_t now) {
    const travel_day_t *day = &s_content.days[s_model.day];
    int16_t minutes[TRAVEL_MAX_REMINDERS] = {-1, -1, -1, -1};
    for (size_t i = 0; i < day->reminder_count; ++i) minutes[i] = day->reminders[i].minute_of_day;
    struct tm local;
    int minute = 0;
    bool valid = current_perth_time(now, &local, &minute);
    return travel_model_next_reminder(minutes, day->reminder_count,
        s_state.completion.completed[s_model.day], valid,
        s_model.preview || s_model.date_state != TRAVEL_DATE_ACTIVE, minute);
}

static void on_button(bsp_btn_t key, bsp_btn_ev_t event, void *user) {
    (void)user;
    if (event != BSP_BTN_CLICK && event != BSP_BTN_LONG) return;
    app_event_t input = {.type = APP_EVENT_BUTTON, .data.button = {key, event}};
    (void)xQueueSend(s_input, &input, 0);
}

static bool content_load(void) {
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "travel_cards");
    const void *data;
    if (partition && esp_partition_mmap(partition, 0, partition->size, ESP_PARTITION_MMAP_DATA, &data, &s_pack_map) == ESP_OK) {
        if (travel_content_open(&s_content, data, partition->size)) {
            ESP_LOGI(TAG, "Loaded replaceable card pack, %u days", (unsigned)s_content.day_count);
            return true;
        }
        esp_partition_munmap(s_pack_map);
        ESP_LOGW(TAG, "Content partition invalid; using built-in Western Australia pack");
    }
    return travel_content_open(&s_content, default_pack_start, (size_t)(default_pack_end - default_pack_start));
}

static void enter_deep_sleep(void) {
    ESP_LOGI(TAG, "Entering deep sleep, wake on GPIO0 button press");
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
    unsigned backlight = 75;
    bool redraw = true;
    time_t last_minute = 0;
    for (;;) {
        app_event_t event;
        if (xQueueReceive(s_input, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
            last_input = now_ms();
            if (backlight != 75) { bsp_display_backlight(75); backlight = 75; }
            if (event.type == APP_EVENT_BUTTON) {
                bsp_btn_t key = event.data.button.key;
                bsp_btn_ev_t key_event = event.data.button.event;
                travel_input_t input = key_event == BSP_BTN_LONG ?
                    (key == BSP_BTN_UP ? TRAVEL_UP_LONG : key == BSP_BTN_DOWN ? TRAVEL_DOWN_LONG : TRAVEL_OK_LONG) :
                    (key == BSP_BTN_UP ? TRAVEL_UP : key == BSP_BTN_DOWN ? TRAVEL_DOWN : TRAVEL_OK);
                const travel_day_t *day = &s_content.days[s_model.day];
                if (input == TRAVEL_DOWN && s_model.page != TRAVEL_REMINDER) {
                    uint8_t reminder = preferred_reminder(time(NULL));
                    s_model.reminder = reminder == TRAVEL_DAY_NONE ? 0 : reminder;
                }
                travel_action_t action = travel_model_input(&s_model, input,
                    TRAVEL_SCHEDULE_CARD_COUNT, day->reminder_count, now_ms());
                if (action == TRAVEL_COMPLETE_REMINDER) {
                    travel_model_complete(&s_model, &s_state.completion);
                    state_save();
                } else if (action == TRAVEL_SAVE_SELECTION || action == TRAVEL_DAY_CHANGED) {
                    save_selection_and_walk(action == TRAVEL_DAY_CHANGED);
                    if (!s_model.preview) redraw |= apply_automatic_date(time(NULL), true);
                } else if (action == TRAVEL_ENTER_DEEP_SLEEP) {
                    enter_deep_sleep();
                }
                redraw = true;
            } else {
                /* Reserved for a future ESP-NOW message source. */
                ESP_LOGD(TAG, "Ignored external event kind %u", event.data.external.kind);
            }
        }

        uint32_t now = now_ms();
        redraw |= travel_model_tick(&s_model, now);
        unsigned next_backlight = travel_backlight_level((uint32_t)(now - last_input), false);
        if (next_backlight != backlight) { bsp_display_backlight(next_backlight); backlight = next_backlight; }
        if (travel_model_should_sleep((uint32_t)(now - last_input))) enter_deep_sleep();

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
            struct tm local;
            int minute = 0;
            bool valid = current_perth_time(current, &local, &minute);
            travel_ui_refresh(&s_content, &s_model, &s_state.completion, battery, minute, valid);
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
    travel_ui_refresh(&s_content, &s_model, &s_state.completion, -1, minute, clock_valid);
    bsp_lvgl_unlock();
    bsp_display_backlight(75);

    s_input = xQueueCreate(12, sizeof(app_event_t));
    if (!s_input || xTaskCreate(input_task, "koala_input", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Input task allocation failed");
        if (s_input) { vQueueDelete(s_input); s_input = NULL; }
        return;
    }
    if (bsp_button_init(on_button, NULL) != ESP_OK) ESP_LOGE(TAG, "Buttons unavailable; homepage remains visible");
    ESP_LOGI(TAG, "UI ready: free heap=%u, largest block=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Ready: UP=schedule, DOWN=reminders, OK=complete/return, hold OK=date, hold DOWN=sleep");
}
