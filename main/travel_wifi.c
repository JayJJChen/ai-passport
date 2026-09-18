/* Application-owned Wi-Fi/BLUFI service. Callbacks only enqueue; no LVGL access. */
#include "travel_wifi.h"
#include "travel_blufi_security.h"
#include "esp_blufi.h"
#include "esp_blufi_api.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include <string.h>

static const char *TAG = "travel_wifi";
static const char *DEVICE_NAME = "BLUFI_Koala"; /* Mini program filters the BLUFI prefix. */
typedef enum { CMD_PAIR, CMD_END, CMD_FORGET, CMD_SSID, CMD_PASSWORD, CMD_CONNECT,
               CMD_DISCONNECT, CMD_SCAN, CMD_REPORT, EV_DISCONNECTED, EV_IP,
               EV_SCAN, EV_ADV, EV_PHONE, EV_PHONE_LEFT, EV_FAILED } command_type_t;
typedef struct { command_type_t type; unsigned length; uint8_t bytes[64]; } command_t;
static QueueHandle_t s_queue;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static travel_net_status_t s_status;
/* All following networking/credential state belongs to the service task. */
static wifi_config_t s_saved, s_candidate, s_active;
static bool s_has_saved, s_radio_initialized, s_radio_started, s_netif_ready, s_loop_ready;
static bool s_wifi_handler_ready, s_ip_handler_ready, s_new_credentials, s_connecting;
static bool s_switching, s_connected, s_pairing, s_phone, s_btc_ready, s_gatt_ready;
static bool s_host_initialized, s_host_running, s_host_stopping, s_host_done;
static volatile bool s_profile_ready;
static SemaphoreHandle_t s_host_stopped;
static TaskHandle_t s_host_task;
static esp_netif_t *s_netif;
static esp_event_handler_instance_t s_wifi_handler, s_ip_handler;
static int64_t s_connect_deadline, s_pair_deadline, s_ble_stop_at;
static unsigned s_retries;
static bool s_radio_cleanup;
static int64_t s_cleanup_retry_at;
static esp_err_t radio_stop(void);

travel_net_status_t travel_wifi_status(void) {
    portENTER_CRITICAL(&s_status_lock);
    travel_net_status_t status = s_status;
    portEXIT_CRITICAL(&s_status_lock);
    return status;
}
static void state(travel_net_state_t value) {
    portENTER_CRITICAL(&s_status_lock);
    s_status = (travel_net_status_t){value, s_pairing, s_has_saved};
    portEXIT_CRITICAL(&s_status_lock);
}
static bool enqueue(command_type_t type, const void *bytes, unsigned length) {
    if (!s_queue || length > 64 || (length && !bytes)) return false;
    command_t command = {.type = type, .length = length};
    if (length) memcpy(command.bytes, bytes, length);
    return xQueueSend(s_queue, &command, 0) == pdTRUE;
}
bool travel_wifi_command(travel_wifi_command_t command) {
    switch (command) {
    case TRAVEL_WIFI_PAIR: return enqueue(CMD_PAIR, NULL, 0);
    case TRAVEL_WIFI_END_PAIR: return enqueue(CMD_END, NULL, 0);
    case TRAVEL_WIFI_FORGET: return enqueue(CMD_FORGET, NULL, 0);
    default: return false;
    }
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)data;
    if (id == WIFI_EVENT_STA_DISCONNECTED) enqueue(EV_DISCONNECTED, NULL, 0);
    if (id == WIFI_EVENT_SCAN_DONE) enqueue(EV_SCAN, NULL, 0);
}
static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)data;
    if (id == IP_EVENT_STA_GOT_IP) enqueue(EV_IP, NULL, 0);
}
static void send_report(void) {
    if (!s_phone || !s_profile_ready) return;
    esp_blufi_extra_info_t info = {0};
    info.sta_ssid = s_active.sta.ssid;
    info.sta_ssid_len = strnlen((const char *)s_active.sta.ssid, sizeof(s_active.sta.ssid));
    esp_blufi_send_wifi_conn_report(WIFI_MODE_STA, s_connected ? ESP_BLUFI_STA_CONN_SUCCESS :
        s_connecting ? ESP_BLUFI_STA_CONNECTING : ESP_BLUFI_STA_CONN_FAIL, 0, &info);
}
static void send_scan(void) {
    if (!s_phone || !s_profile_ready) return;
    uint16_t count = 8;
    wifi_ap_record_t records[8] = {0}; esp_blufi_ap_record_t list[8] = {0};
    if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
        esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL); return;
    }
    for (unsigned i = 0; i < count; ++i) { list[i].rssi = records[i].rssi; memcpy(list[i].ssid, records[i].ssid, sizeof(list[i].ssid)); }
    esp_blufi_send_wifi_list(count, list);
}
static void blufi_reset(int reason) { (void)reason; enqueue(EV_FAILED, NULL, 0); }
static void blufi_sync(void) {
    if (esp_blufi_profile_init() == 0) s_profile_ready = true;
    else enqueue(EV_FAILED, NULL, 0);
}
static void host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    /* No shared accesses after acknowledgement; owner then deletes this parked task. */
    xSemaphoreGive(s_host_stopped);
    vTaskSuspend(NULL);
}
static void blufi_event(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param) {
    bool ok = true;
    switch (event) {
    case ESP_BLUFI_EVENT_INIT_FINISH: ok = enqueue(EV_ADV, NULL, 0); break;
    case ESP_BLUFI_EVENT_BLE_CONNECT:
        if (travel_blufi_security_init() != 0) ok = enqueue(EV_FAILED, NULL, 0);
        else ok = enqueue(EV_PHONE, NULL, 0);
        break;
    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        travel_blufi_security_deinit(); ok = enqueue(EV_PHONE_LEFT, NULL, 0); break;
    case ESP_BLUFI_EVENT_RECV_STA_SSID:
        ok = param->sta_ssid.ssid_len > 0 && param->sta_ssid.ssid_len <= 32 &&
             enqueue(CMD_SSID, param->sta_ssid.ssid, param->sta_ssid.ssid_len); break;
    case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        ok = param->sta_passwd.passwd_len <= 64 && enqueue(CMD_PASSWORD, param->sta_passwd.passwd, param->sta_passwd.passwd_len); break;
    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP: ok = enqueue(CMD_CONNECT, NULL, 0); break;
    case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP: ok = enqueue(CMD_DISCONNECT, NULL, 0); break;
    case ESP_BLUFI_EVENT_GET_WIFI_LIST: ok = enqueue(CMD_SCAN, NULL, 0); break;
    case ESP_BLUFI_EVENT_GET_WIFI_STATUS: ok = enqueue(CMD_REPORT, NULL, 0); break;
    case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE: esp_blufi_disconnect(); break;
    case ESP_BLUFI_EVENT_REPORT_ERROR: esp_blufi_send_error_info(param->report_error.state); break;
    default: break;
    }
    if (!ok) esp_blufi_send_error_info(ESP_BLUFI_DATA_FORMAT_ERROR);
}
static esp_blufi_callbacks_t s_callbacks = {
    .event_cb = blufi_event, .negotiate_data_handler = travel_blufi_negotiate,
    .encrypt_func = travel_blufi_encrypt, .decrypt_func = travel_blufi_decrypt,
    .checksum_func = travel_blufi_checksum,
};
static esp_err_t radio_start(void) {
    if (s_radio_cleanup && radio_stop() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (s_radio_started) return ESP_OK;
    esp_err_t err;
    if (!s_netif_ready) { if ((err = esp_netif_init()) != ESP_OK) return err; s_netif_ready = true; }
    if (!s_loop_ready) { if ((err = esp_event_loop_create_default()) != ESP_OK) return err; s_loop_ready = true; }
    if (!s_netif) {
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_WIFI_STA();
        s_netif = esp_netif_new(&cfg);
        if (!s_netif) return ESP_ERR_NO_MEM;
        err = esp_netif_attach_wifi_station(s_netif);
        if (err == ESP_OK) err = esp_wifi_set_default_wifi_sta_handlers();
        if (err != ESP_OK) { esp_netif_destroy_default_wifi(s_netif); s_netif = NULL; return err; }
    }
    if (!s_radio_initialized) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        if ((err = esp_wifi_init(&cfg)) != ESP_OK) return err;
        s_radio_initialized = true;
    }
    if (!s_wifi_handler_ready) {
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, &s_wifi_handler);
        if (err != ESP_OK) return err;
        s_wifi_handler_ready = true;
    }
    if (!s_ip_handler_ready) {
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event, NULL, &s_ip_handler);
        if (err != ESP_OK) return err;
        s_ip_handler_ready = true;
    }
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK ||
        (err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK || (err = esp_wifi_start()) != ESP_OK) return err;
    s_radio_started = true;
    return ESP_OK;
}
static esp_err_t host_start(void) {
    if (s_host_initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_blufi_register_callbacks(&s_callbacks);
    if (err != ESP_OK || (err = nimble_port_init()) != ESP_OK) return err;
    s_host_initialized = true;
    s_host_stopped = xSemaphoreCreateBinary();
    if (!s_host_stopped) return ESP_ERR_NO_MEM;
    ble_hs_cfg.reset_cb = blufi_reset; ble_hs_cfg.sync_cb = blufi_sync;
    ble_hs_cfg.gatts_register_cb = esp_blufi_gatt_svr_register_cb;
    if (esp_blufi_gatt_svr_init() != 0) return ESP_FAIL;
    s_gatt_ready = true;
    if (ble_svc_gap_device_name_set(DEVICE_NAME) != 0) return ESP_FAIL;
    esp_blufi_btc_init(); s_btc_ready = true;
    /* The SDK's void task-launch helper does not report allocation failure. */
    if (xTaskCreate(host_task, "koala_blufi", CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE, NULL, 5, &s_host_task) != pdPASS) return ESP_ERR_NO_MEM;
    s_host_running = true;
    return ESP_OK;
}
static esp_err_t host_stop(void) {
    if (!s_host_initialized) return ESP_OK;
    if (s_profile_ready) esp_blufi_adv_stop();
    if (s_host_running && !s_host_stopping) {
        if (nimble_port_stop() != 0) return ESP_FAIL;
        s_host_stopping = true;
    }
    if (s_host_running && !s_host_done) {
        if (xSemaphoreTake(s_host_stopped, pdMS_TO_TICKS(1500)) != pdTRUE) return ESP_ERR_TIMEOUT;
        s_host_done = true;
        vTaskDelete(s_host_task); s_host_task = NULL;
    }
    if (s_gatt_ready) { esp_blufi_gatt_svr_deinit(); s_gatt_ready = false; }
    if (s_profile_ready) { esp_blufi_profile_deinit(); s_profile_ready = false; }
    if (s_btc_ready) { esp_blufi_btc_deinit(); s_btc_ready = false; }
    esp_err_t err = nimble_port_deinit();
    if (err != ESP_OK) return err;
    travel_blufi_security_deinit();
    if (s_host_stopped) { vSemaphoreDelete(s_host_stopped); s_host_stopped = NULL; }
    s_host_initialized = s_host_running = s_host_stopping = s_host_done = s_phone = false;
    return ESP_OK;
}
static esp_err_t radio_stop(void) {
    /* Retain ownership after failed SDK cleanup; retry without freeing live state. */
    s_radio_cleanup = true; s_cleanup_retry_at = esp_timer_get_time() + 1000000;
    s_connected = s_connecting = s_switching = false;
    esp_err_t err;
    if (s_radio_started) {
        esp_wifi_scan_stop(); esp_wifi_disconnect(); err = esp_wifi_stop();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) return err;
        s_radio_started = false;
    }
    if (s_ip_handler_ready) {
        err = esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
        if (err != ESP_OK) return err;
        s_ip_handler_ready = false;
    }
    if (s_wifi_handler_ready) {
        err = esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
        if (err != ESP_OK) return err;
        s_wifi_handler_ready = false;
    }
    if (s_radio_initialized) {
        err = esp_wifi_deinit();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) return err;
        s_radio_initialized = false;
    }
    if (s_netif) { esp_netif_destroy_default_wifi(s_netif); s_netif = NULL; }
    s_radio_cleanup = false;
    return ESP_OK;
}
static esp_err_t save(const wifi_config_t *config) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("koala_wifi", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = config ? nvs_set_blob(handle, "network", config, sizeof(*config)) : nvs_erase_key(handle, "network");
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle); return err;
}
static void apply_connection(void) {
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &s_active);
    if (err == ESP_OK) err = esp_wifi_connect();
    if (err != ESP_OK) { s_connecting = false; state(TRAVEL_NET_FAILED); send_report(); }
}
static void connect(const wifi_config_t *config, bool replacement) {
    if (!config->sta.ssid[0]) { state(TRAVEL_NET_FAILED); return; }
    if (radio_start() != ESP_OK) { radio_stop(); state(TRAVEL_NET_FAILED); return; }
    s_active = *config; s_new_credentials = replacement;
    s_connecting = true; s_connected = false; s_retries = 0;
    s_connect_deadline = esp_timer_get_time() + 10000000;
    state(TRAVEL_NET_CONNECTING);
    s_switching = esp_wifi_disconnect() == ESP_OK;
    if (!s_switching) apply_connection();
}
static void end_pair(void) {
    s_pairing = false; s_pair_deadline = 0;
    if (host_stop() != ESP_OK) { s_ble_stop_at = esp_timer_get_time() + 1000000; state(TRAVEL_NET_FAILED); return; }
    s_ble_stop_at = 0;
    if (!s_connected) {
        radio_stop();
        /* A bad new password never overwrites the last working network. */
        if (s_has_saved) connect(&s_saved, false); else state(TRAVEL_NET_OFFLINE);
    } else state(TRAVEL_NET_CONNECTED);
    memset(&s_candidate, 0, sizeof(s_candidate));
}
static void process(const command_t *command) {
    switch (command->type) {
    case CMD_PAIR:
        if (s_pairing) break;
        if (s_host_initialized) { state(TRAVEL_NET_FAILED); break; }
        s_pairing = true; s_pair_deadline = esp_timer_get_time() + 120000000;
        memset(&s_candidate, 0, sizeof(s_candidate));
        if (radio_start() != ESP_OK || host_start() != ESP_OK) { end_pair(); state(TRAVEL_NET_FAILED); }
        else {
            state(TRAVEL_NET_PAIRING);
            ESP_LOGI(TAG, "Pairing: free heap=%u, largest block=%u",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        }
        break;
    case CMD_END: end_pair(); break;
    case CMD_FORGET:
        if (save(NULL) != ESP_OK) { state(TRAVEL_NET_FAILED); break; }
        s_has_saved = false; memset(&s_saved, 0, sizeof(s_saved)); end_pair(); radio_stop();
        memset(&s_active, 0, sizeof(s_active)); state(TRAVEL_NET_OFFLINE); break;
    case CMD_SSID:
        if (!s_pairing) break;
        memset(&s_candidate, 0, sizeof(s_candidate)); memcpy(s_candidate.sta.ssid, command->bytes, command->length); break;
    case CMD_PASSWORD:
        if (!s_pairing) break;
        memset(s_candidate.sta.password, 0, sizeof(s_candidate.sta.password));
        memcpy(s_candidate.sta.password, command->bytes, command->length); break;
    case CMD_CONNECT: if (s_pairing) connect(&s_candidate, true); break;
    case CMD_DISCONNECT: s_connecting = false; s_connected = false; s_switching = false; esp_wifi_disconnect(); break;
    case CMD_SCAN:
        if (s_pairing) { wifi_scan_config_t scan = {0}; if (esp_wifi_scan_start(&scan, false) != ESP_OK) esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL); }
        break;
    case CMD_REPORT: send_report(); break;
    case EV_DISCONNECTED:
        if (!s_radio_started) break;
        s_connected = false;
        if (s_switching) { s_switching = false; apply_connection(); break; }
        if (s_connecting && ++s_retries <= 2 && esp_timer_get_time() < s_connect_deadline) { esp_wifi_connect(); break; }
        s_connecting = false; state(s_pairing ? TRAVEL_NET_FAILED : TRAVEL_NET_OFFLINE); send_report();
        if (!s_pairing) radio_stop();
        break;
    case EV_IP:
        if (!s_radio_started || !s_connecting || s_switching) break;
        /* An old DHCP event must not save a newly entered, untested password. */
        wifi_ap_record_t associated = {0};
        if (esp_wifi_sta_get_ap_info(&associated) != ESP_OK ||
            strncmp((const char *)associated.ssid, (const char *)s_active.sta.ssid, sizeof(s_active.sta.ssid))) break;
        s_connected = true; s_connecting = false;
        if (s_new_credentials) {
            if (save(&s_active) == ESP_OK) { s_saved = s_active; s_has_saved = true; }
            else ESP_LOGW(TAG, "Network connected but credentials could not be saved");
            s_new_credentials = false;
        }
        state(TRAVEL_NET_CONNECTED); send_report();
        if (s_pairing) s_ble_stop_at = esp_timer_get_time() + 1500000;
        break;
    case EV_SCAN: send_scan(); break;
    case EV_ADV: if (s_pairing) { esp_blufi_adv_start_with_name(DEVICE_NAME); if (!s_connected && !s_connecting) state(TRAVEL_NET_PAIRING); } break;
    case EV_PHONE: s_phone = true; esp_blufi_adv_stop(); if (!s_connected && !s_connecting) state(TRAVEL_NET_PHONE); break;
    case EV_PHONE_LEFT:
        s_phone = false;
        if (s_pairing) { esp_blufi_adv_start_with_name(DEVICE_NAME); if (!s_connected && !s_connecting) state(TRAVEL_NET_PAIRING); }
        break;
    case EV_FAILED: end_pair(); state(TRAVEL_NET_FAILED); break;
    }
}
static void service_task(void *arg) {
    (void)arg;
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS unavailable; preserved existing data: %s", esp_err_to_name(err));
        state(TRAVEL_NET_FAILED);
        /* Keep the command task alive; failures remain visible without touching NVS. */
        for (;;) { command_t ignored; xQueueReceive(s_queue, &ignored, portMAX_DELAY); state(TRAVEL_NET_FAILED); }
    }
    nvs_handle_t handle;
    if (nvs_open("koala_wifi", NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(s_saved);
        s_has_saved = nvs_get_blob(handle, "network", &s_saved, &size) == ESP_OK && size == sizeof(s_saved) && s_saved.sta.ssid[0];
        nvs_close(handle);
    }
    if (s_has_saved) connect(&s_saved, false); else state(TRAVEL_NET_OFFLINE);
    for (;;) {
        command_t command;
        if (xQueueReceive(s_queue, &command, pdMS_TO_TICKS(100)) == pdTRUE) process(&command);
        int64_t now = esp_timer_get_time();
        if (s_radio_cleanup && now >= s_cleanup_retry_at) (void)radio_stop();
        if (s_ble_stop_at && now >= s_ble_stop_at) end_pair();
        if (s_pairing && s_pair_deadline && now >= s_pair_deadline) { end_pair(); state(TRAVEL_NET_TIMEOUT); }
        if (s_connecting && now >= s_connect_deadline) {
            s_connecting = s_switching = false; esp_wifi_disconnect(); send_report();
            state(s_pairing ? TRAVEL_NET_FAILED : TRAVEL_NET_OFFLINE);
            if (!s_pairing) radio_stop();
        }
    }
}
esp_err_t travel_wifi_init(void) {
    if (s_queue) return ESP_OK;
    s_queue = xQueueCreate(24, sizeof(command_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(service_task, "koala_wifi", 6144, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_queue); s_queue = NULL; return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
