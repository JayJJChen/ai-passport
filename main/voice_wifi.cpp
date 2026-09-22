#include "voice_wifi.h"

#include <cstring>
#include <string>
#include "esp_log.h"
#include "ssid_manager.h"
#include "wifi_manager.h"

static const char *TAG = "voice_wifi";
static voice_wifi_callback_t s_callback;
static void *s_context;
static volatile voice_wifi_state_t s_state = VOICE_WIFI_OFFLINE;

static void publish(voice_wifi_state_t state) {
    s_state = state;
    if (s_callback) s_callback(state, s_context);
}

extern "C" esp_err_t voice_wifi_init(voice_wifi_callback_t callback, void *context) {
    s_callback = callback;
    s_context = context;

    WifiManagerConfig config;
    config.ssid_prefix = "Koala";
    config.language = "zh-CN";
    config.station_scan_min_interval_seconds = 5;
    config.station_scan_max_interval_seconds = 120;
    config.station_failure_retry_cnt = 3;
    config.station_hostname = "koala-passport";
    config.show_ota_config = false;
    config.show_sleep_config = false;

    auto &manager = WifiManager::GetInstance();
    if (!manager.Initialize(config)) return ESP_FAIL;
    manager.SetEventCallback([](WifiEvent event, const std::string &) {
        switch (event) {
        case WifiEvent::Scanning:
        case WifiEvent::Connecting:
            publish(VOICE_WIFI_CONNECTING);
            break;
        case WifiEvent::Connected:
            publish(VOICE_WIFI_ONLINE);
            break;
        case WifiEvent::Disconnected:
            publish(VOICE_WIFI_OFFLINE);
            break;
        case WifiEvent::ConfigModeEnter:
            publish(VOICE_WIFI_CONFIGURING);
            break;
        case WifiEvent::ConfigModeExit:
            publish(VOICE_WIFI_CONNECTING);
            WifiManager::GetInstance().StartStation();
            break;
        }
    });

    if (SsidManager::GetInstance().GetSsidList().empty()) {
        ESP_LOGI(TAG, "No saved Wi-Fi; starting captive portal");
        manager.StartConfigAp();
    } else {
        publish(VOICE_WIFI_CONNECTING);
        manager.StartStation();
    }
    return ESP_OK;
}

extern "C" bool voice_wifi_is_connected(void) {
    return WifiManager::GetInstance().IsConnected();
}

extern "C" voice_wifi_state_t voice_wifi_get_state(void) {
    return s_state;
}

extern "C" void voice_wifi_start_configuration(void) {
    WifiManager::GetInstance().StartConfigAp();
}

extern "C" size_t voice_wifi_device_id(char *destination, size_t capacity) {
    if (!destination || capacity == 0) return 0;
    std::string id = WifiManager::GetInstance().GetMacAddress();
    if (id.empty() || id.size() + 1 > capacity) {
        destination[0] = '\0';
        return 0;
    }
    std::memcpy(destination, id.c_str(), id.size() + 1);
    return id.size();
}
