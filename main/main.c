#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "travel_content.h"
#include "travel_model.h"
#include "travel_ui.h"
#include "travel_wifi.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "koala";
extern const uint8_t default_pack_start[] asm("_binary_koala_default_start");
extern const uint8_t default_pack_end[] asm("_binary_koala_default_end");
static travel_content_t s_content;
static travel_model_t s_model;
static QueueHandle_t s_input;
static esp_partition_mmap_handle_t s_pack_map;
static bool s_battery_ready;
typedef struct { bsp_btn_t key; bsp_btn_ev_t event; } key_event_t;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void on_button(bsp_btn_t key, bsp_btn_ev_t event, void *user) {
    (void)user;
    if (event != BSP_BTN_CLICK && !(key == BSP_BTN_OK && event == BSP_BTN_LONG)) return;
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
            travel_input_t input = event.event == BSP_BTN_LONG ? TRAVEL_OK_LONG :
                                   event.key == BSP_BTN_UP ? TRAVEL_UP : event.key == BSP_BTN_DOWN ? TRAVEL_DOWN : TRAVEL_OK;
            const travel_place_t *place = &s_content.places[s_model.place];
            travel_action_t action = travel_model_input(&s_model, input, s_content.place_count,
                place->greeting_count, place->task_count, now_ms());
            if (action == TRAVEL_START_PROVISION) travel_wifi_command(TRAVEL_WIFI_PAIR);
            if (action == TRAVEL_STOP_PROVISION) travel_wifi_command(TRAVEL_WIFI_END_PAIR);
            if (action == TRAVEL_FORGET_WIFI) travel_wifi_command(TRAVEL_WIFI_FORGET);
            redraw = true;
        }
        uint32_t now = now_ms();
        redraw |= travel_model_tick(&s_model, now);
        travel_net_status_t fresh = travel_wifi_status();
        redraw |= status_changed(network, fresh); network = fresh;
        unsigned next_backlight = travel_backlight_level((uint32_t)(now - last_input),
            s_model.page == TRAVEL_PROVISION && network.provisioning);
        if (next_backlight != backlight) { bsp_display_backlight(next_backlight); backlight = next_backlight; }
        if ((uint32_t)(now - battery_time) >= 30000) {
            int fresh_battery = s_battery_ready ? bsp_battery_soc() : -1;
            redraw |= fresh_battery != battery; battery = fresh_battery; battery_time = now;
        }
        if (redraw && bsp_lvgl_lock(200)) {
            travel_ui_refresh(&s_content, &s_model, network, battery);
            bsp_lvgl_unlock(); redraw = false;
        }
    }
}
void app_main(void) {
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
    travel_ui_refresh(&s_content, &s_model, (travel_net_status_t){0}, -1);
    bsp_lvgl_unlock();
    bsp_display_backlight(75);
    /* No audio/network initialization is needed to display or explore the homepage. */
    s_input = xQueueCreate(12, sizeof(key_event_t));
    if (!s_input || xTaskCreate(input_task, "koala_input", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Input task allocation failed"); if (s_input) { vQueueDelete(s_input); s_input = NULL; } return;
    }
    if (bsp_button_init(on_button, NULL) != ESP_OK) ESP_LOGE(TAG, "Buttons unavailable; homepage remains visible");
    if (travel_wifi_init() != ESP_OK) ESP_LOGW(TAG, "Wi-Fi service unavailable; offline exploration remains available");
    ESP_LOGI(TAG, "UI ready: free heap=%u, largest block=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Ready; short UP=greeting, DOWN=explore, OK=respond; hold OK 3s=parent settings");
}
