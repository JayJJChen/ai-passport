#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "bsp_audio.h"
#include "bsp_pins.h"
#include "travel_content.h"
#include "travel_model.h"
#include "travel_ui.h"
#include "travel_wifi.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include <sys/time.h>
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "koala";
extern const uint8_t default_pack_start[] asm("_binary_koala_default_start");
extern const uint8_t default_pack_end[] asm("_binary_koala_default_end");
static travel_content_t s_content;
static travel_model_t s_model;
typedef struct { bsp_btn_t key; bsp_btn_ev_t event; } key_event_t;
static QueueHandle_t s_input;
static esp_partition_mmap_handle_t s_pack_map;
static bool s_battery_ready;
static travel_stamp_record_t s_stamps;
static travel_custom_schedule_t s_custom_schedule;
static uint32_t s_sync_auto_exit_time;

static void nvs_load_data(void) {
    nvs_handle_t handle;
    if (nvs_open("passport", NVS_READWRITE, &handle) == ESP_OK) {
        size_t sz = sizeof(s_stamps);
        if (nvs_get_blob(handle, "stamps", &s_stamps, &sz) != ESP_OK || sz != sizeof(s_stamps)) {
            memset(&s_stamps, 0, sizeof(s_stamps));
        }
        sz = sizeof(s_custom_schedule);
        if (nvs_get_blob(handle, "custom_trip", &s_custom_schedule, &sz) != ESP_OK || sz != sizeof(s_custom_schedule)) {
            memset(&s_custom_schedule, 0, sizeof(s_custom_schedule));
        }
        nvs_close(handle);
    }
}

static void nvs_save_stamps(void) {
    nvs_handle_t handle;
    if (nvs_open("passport", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_blob(handle, "stamps", &s_stamps, sizeof(s_stamps));
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Saved stamps to NVS, mask=0x%lx", (unsigned long)s_stamps.mask);
    }
}

static void nvs_save_schedule(void) {
    nvs_handle_t handle;
    if (nvs_open("passport", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_blob(handle, "custom_trip", &s_custom_schedule, sizeof(s_custom_schedule));
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Saved custom schedule to NVS: %s", s_custom_schedule.title);
    }
}

static void on_wifi_sync(time_t epoch, const travel_custom_schedule_t *schedule) {
    (void)epoch;
    if (schedule) {
        memcpy(&s_custom_schedule, schedule, sizeof(s_custom_schedule));
        nvs_save_schedule();
        travel_wifi_set_current_trip(&s_custom_schedule);
    }
    key_event_t ev = { .key = (bsp_btn_t)99, .event = (bsp_btn_ev_t)99 };
    xQueueSend(s_input, &ev, 0);
}

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void on_button(bsp_btn_t key, bsp_btn_ev_t event, void *user) {
    (void)user;
    if (event != BSP_BTN_CLICK && event != BSP_BTN_LONG) return;
    key_event_t input = {key, event};
    (void)xQueueSend(s_input, &input, 0);
}
static bool content_load(void) {
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "travel_cards");
    const void *data;
    if (partition && esp_partition_mmap(partition, 0, partition->size, ESP_PARTITION_MMAP_DATA, &data, &s_pack_map) == ESP_OK) {
        if (travel_content_open(&s_content, data, partition->size)) {
            ESP_LOGI(TAG, "Loaded replaceable card pack, %u destinations", (unsigned)s_content.place_count); return true;
        }
        esp_partition_munmap(s_pack_map);
        ESP_LOGW(TAG, "Content partition invalid; using built-in Shanghai pack");
    }
    return travel_content_open(&s_content, default_pack_start, (size_t)(default_pack_end - default_pack_start));
}
static bool status_changed(travel_net_status_t a, travel_net_status_t b) {
    return a.state != b.state || a.provisioning != b.provisioning || a.saved != b.saved;
}
static void enter_deep_sleep(void) {
    ESP_LOGI(TAG, "Entering terminal deep sleep, wake on GPIO0 button press");
    if (s_battery_ready) {
        bsp_battery_sleep();
    }
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
static void input_task(void *arg) {
    (void)arg;
    int battery = s_battery_ready ? bsp_battery_soc() : -1;
    uint32_t battery_time = now_ms();
    uint32_t last_input = battery_time;
    unsigned backlight = 75;
    travel_net_status_t network = travel_wifi_status();
    bool redraw = true;
    for (;;) {
        key_event_t event;
        if (xQueueReceive(s_input, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
            last_input = now_ms();
            if (backlight != 75) { bsp_display_backlight(75); backlight = 75; }
            if (event.key == 99 && event.event == 99) {
                if (bsp_lvgl_lock(200)) {
                    travel_ui_show_sync_success("新行程已就绪");
                    bsp_lvgl_unlock();
                }
                s_sync_auto_exit_time = now_ms() + 2000;
            } else {
                travel_input_t input;
                if (event.event == BSP_BTN_LONG) {
                    input = (event.key == BSP_BTN_UP) ? TRAVEL_UP_LONG :
                            (event.key == BSP_BTN_DOWN) ? TRAVEL_DOWN_LONG : TRAVEL_OK_LONG;
                } else {
                    input = (event.key == BSP_BTN_UP) ? TRAVEL_UP :
                            (event.key == BSP_BTN_DOWN) ? TRAVEL_DOWN : TRAVEL_OK;
                }
                const travel_place_t *place = &s_content.places[s_model.place];
                size_t task_count = (s_custom_schedule.is_custom && s_custom_schedule.task_count > 0)
                                  ? s_custom_schedule.task_count : place->task_count;
                travel_action_t action = travel_model_input(&s_model, input, s_content.place_count,
                    place->greeting_count, task_count, now_ms());
                if (action == TRAVEL_START_SOFTAP) {
                    travel_wifi_start_softap();
                } else if (action == TRAVEL_STOP_SOFTAP) {
                    travel_wifi_stop_softap();
                    s_sync_auto_exit_time = 0;
                } else if (action == TRAVEL_STAMP_CURRENT) {
                    travel_model_stamp(&s_model, &s_stamps, (uint32_t)time(NULL));
                    nvs_save_stamps();
                } else if (action == TRAVEL_ENTER_DEEP_SLEEP) {
                    enter_deep_sleep();
                }
            }
            redraw = true;
        }
        if (s_sync_auto_exit_time > 0 && now_ms() >= s_sync_auto_exit_time) {
            s_sync_auto_exit_time = 0;
            travel_wifi_stop_softap();
            s_model.page = TRAVEL_HOME;
            redraw = true;
        }
        uint32_t now = now_ms();
        redraw |= travel_model_tick(&s_model, now);
        travel_net_status_t fresh = travel_wifi_status();
        redraw |= status_changed(network, fresh); network = fresh;
        unsigned next_backlight = travel_backlight_level((uint32_t)(now - last_input),
            s_model.page == TRAVEL_PROVISION && network.provisioning);
        if (next_backlight != backlight) { bsp_display_backlight(next_backlight); backlight = next_backlight; }
        if (travel_model_should_sleep((uint32_t)(now - last_input))) {
            enter_deep_sleep();
        }
        static time_t s_last_time_sec;
        time_t current_sec = time(NULL);
        if (current_sec / 60 != s_last_time_sec / 60) {
            s_last_time_sec = current_sec;
            char tbuf[20];
            travel_model_format_time(current_sec, tbuf, sizeof(tbuf));
            if (bsp_lvgl_lock(100)) {
                travel_ui_set_time(tbuf);
                bsp_lvgl_unlock();
            }
        }
        if ((uint32_t)(now - battery_time) >= 30000) {
            int fresh_battery = s_battery_ready ? bsp_battery_soc() : -1;
            redraw |= fresh_battery != battery; battery = fresh_battery; battery_time = now;
        }
        if (redraw && bsp_lvgl_lock(200)) {
            travel_ui_refresh(&s_content, &s_model, network, battery, &s_stamps, &s_custom_schedule);
            bsp_lvgl_unlock(); redraw = false;
        }
    }
}
void app_main(void) {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Boot cause: %d", (int)cause);
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    nvs_load_data();
    time_t current_time = time(NULL);
    if (current_time < 1700000000) {
        struct timeval tv = { .tv_sec = 1789812000, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        current_time = tv.tv_sec;
    }
    ESP_LOGI(TAG, "Koala travel companion: Shanghai, offline-first");
    bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "Display initialization failed"); return;
    }
    s_battery_ready = bsp_battery_init() == ESP_OK;
    if (!bsp_lvgl_lock(2000)) { ESP_LOGE(TAG, "Initial UI lock unavailable"); return; }
    if (!content_load()) { bsp_lvgl_unlock(); ESP_LOGE(TAG, "Default card pack could not be loaded"); return; }
    travel_model_init(&s_model);
    travel_ui_create(&s_content);
    travel_ui_refresh(&s_content, &s_model, (travel_net_status_t){0}, -1, &s_stamps, &s_custom_schedule);
    char initial_time[20];
    travel_model_format_time(current_time, initial_time, sizeof(initial_time));
    travel_ui_set_time(initial_time);
    bsp_lvgl_unlock();
    bsp_display_backlight(75);
    s_input = xQueueCreate(12, sizeof(key_event_t));
    if (!s_input || xTaskCreate(input_task, "koala_input", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Input task allocation failed"); if (s_input) { vQueueDelete(s_input); s_input = NULL; } return;
    }
    if (bsp_button_init(on_button, NULL) != ESP_OK) ESP_LOGE(TAG, "Buttons unavailable; homepage remains visible");
    travel_wifi_set_current_trip(&s_custom_schedule);
    if (travel_wifi_init(on_wifi_sync) != ESP_OK) ESP_LOGW(TAG, "Wi-Fi service unavailable; offline exploration remains available");
    ESP_LOGI(TAG, "UI ready: free heap=%u, largest block=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Ready: short UP=greeting, DOWN=explore, OK=stamp; hold OK 3s=passport album; hold UP 3s=web sync");
}
