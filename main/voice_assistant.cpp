#include "voice_assistant.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <atomic>
#include <sys/time.h>
#include "cJSON.h"
#include "bsp_audio.h"
#include "esp_audio_enc.h"
#include "esp_audio_types.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_opus_dec.h"
#include "esp_opus_enc.h"
#include "esp_xiaozhi_chat.h"
#include "esp_xiaozhi_info.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "travel_model.h"
#include "voice_wifi.h"

namespace {

constexpr int kSampleRate = 16000;
constexpr int kFrameDurationMs = 60;
constexpr size_t kPcmSamples = kSampleRate * kFrameDurationMs / 1000;
constexpr size_t kMaxOpusBytes = 1024;
constexpr size_t kStatePayloadBytes = 896;
constexpr EventBits_t kNetworkOnline = BIT0;
constexpr EventBits_t kCapturing = BIT1;
constexpr EventBits_t kTransportConnected = BIT2;
constexpr EventBits_t kChannelReady = BIT3;
constexpr EventBits_t kSendAllowed = BIT4;
constexpr EventBits_t kDropPlayback = BIT5;
constexpr EventBits_t kSpeaking = BIT6;
constexpr EventBits_t kProgressRefresh = BIT7;

struct OpusPacket {
    uint16_t size;
    uint8_t data[kMaxOpusBytes];
};

enum class CommandType : uint8_t {
    NetworkUp,
    NetworkDown,
    Start,
    Stop,
    Suspend,
};

struct Command {
    CommandType type;
    uint32_t turn;
    uint16_t payload_size;
    char payload[kStatePayloadBytes];
};

const char *TAG = "voice";
EventGroupHandle_t s_events;
QueueHandle_t s_commands;
QueueHandle_t s_tx_packets;
QueueHandle_t s_rx_packets;
SemaphoreHandle_t s_suspend_done;
TaskHandle_t s_control_task;
TaskHandle_t s_capture_task;
TaskHandle_t s_sender_task;
TaskHandle_t s_playback_task;
esp_xiaozhi_chat_handle_t s_chat;
void *s_encoder;
void *s_decoder;
int s_encoder_frame_bytes;
int s_encoder_output_bytes;
voice_assistant_status_callback_t s_status_callback;
void *s_status_context;
volatile voice_assistant_status_t s_status = VOICE_ASSISTANT_OFFLINE;
bool s_event_handler_registered;
voice_progress_callback_t s_progress_callback;
void *s_progress_context;
std::atomic<uint32_t> s_requested_turn{0};
std::atomic<uint32_t> s_stop_fallback{0};
uint32_t s_running_turn; /* control worker only */
bool s_running_listening;

void publish(voice_assistant_status_t status) {
    s_status = status;
    if (s_status_callback) s_status_callback(status, s_status_context);
}

void reset_audio_flow() {
    if (!s_events) return;
    xEventGroupClearBits(s_events, kCapturing | kSendAllowed | kChannelReady | kSpeaking);
    xEventGroupSetBits(s_events, kDropPlayback);
    if (s_tx_packets) xQueueReset(s_tx_packets);
    if (s_rx_packets) xQueueReset(s_rx_packets);
}

void protocol_event(esp_xiaozhi_chat_event_t event, void *event_data, void *) {
    switch (event) {
    case ESP_XIAOZHI_CHAT_EVENT_CHAT_SPEECH_STARTED:
        xEventGroupSetBits(s_events, kSpeaking);
        publish(VOICE_ASSISTANT_SPEAKING);
        break;
    case ESP_XIAOZHI_CHAT_EVENT_CHAT_SPEECH_STOPPED:
        xEventGroupClearBits(s_events, kSpeaking);
        xEventGroupSetBits(s_events, kProgressRefresh);
        if (xEventGroupGetBits(s_events) & kNetworkOnline) publish(VOICE_ASSISTANT_READY);
        break;
    case ESP_XIAOZHI_CHAT_EVENT_CHAT_TTS_STATE: {
        auto *state = static_cast<esp_xiaozhi_chat_tts_state_t *>(event_data);
        if (!state) break;
        if (state->state == ESP_XIAOZHI_CHAT_TTS_STATE_START) {
            xEventGroupSetBits(s_events, kSpeaking);
            publish(VOICE_ASSISTANT_SPEAKING);
        } else if (state->state == ESP_XIAOZHI_CHAT_TTS_STATE_STOP) {
            xEventGroupClearBits(s_events, kSpeaking);
            xEventGroupSetBits(s_events, kProgressRefresh);
            if (xEventGroupGetBits(s_events) & kNetworkOnline) publish(VOICE_ASSISTANT_READY);
        }
        break;
    }
    case ESP_XIAOZHI_CHAT_EVENT_CHAT_ERROR:
        xEventGroupSetBits(s_events, kProgressRefresh);
        publish(VOICE_ASSISTANT_ERROR);
        break;
    default:
        break;  // STT/LLM text is intentionally not rendered on the fixed-glyph UI.
    }
}

void incoming_audio(const uint8_t *data, int length, void *) {
    if (!data || length <= 0 || (size_t)length > kMaxOpusBytes ||
        (xEventGroupGetBits(s_events) & kDropPlayback)) return;
    OpusPacket packet = {};
    packet.size = (uint16_t)length;
    std::memcpy(packet.data, data, (size_t)length);
    if (xQueueSend(s_rx_packets, &packet, 0) != pdTRUE) {
        OpusPacket discarded;
        (void)xQueueReceive(s_rx_packets, &discarded, 0);
        (void)xQueueSend(s_rx_packets, &packet, 0);
    }
}

void system_event(void *, esp_event_base_t, int32_t event_id, void *) {
    switch (event_id) {
    case ESP_XIAOZHI_CHAT_EVENT_CONNECTED:
        xEventGroupSetBits(s_events, kTransportConnected);
        break;
    case ESP_XIAOZHI_CHAT_EVENT_DISCONNECTED:
    case ESP_XIAOZHI_CHAT_EVENT_SERVER_GOODBYE:
        xEventGroupClearBits(s_events, kTransportConnected | kChannelReady | kSendAllowed | kSpeaking);
        xEventGroupSetBits(s_events, kDropPlayback | kProgressRefresh);
        if (xEventGroupGetBits(s_events) & kNetworkOnline) publish(VOICE_ASSISTANT_ERROR);
        break;
    case ESP_XIAOZHI_CHAT_EVENT_AUDIO_CHANNEL_OPENED:
        xEventGroupSetBits(s_events, kChannelReady);
        break;
    case ESP_XIAOZHI_CHAT_EVENT_AUDIO_CHANNEL_CLOSED:
        xEventGroupClearBits(s_events, kChannelReady | kSendAllowed);
        break;
    default:
        break;
    }
}

esp_err_t initialize_codecs() {
    esp_opus_enc_config_t encoder_config = {
        .sample_rate = ESP_AUDIO_SAMPLE_RATE_16K,
        .channel = ESP_AUDIO_MONO,
        .bits_per_sample = ESP_AUDIO_BIT16,
        .bitrate = ESP_OPUS_BITRATE_AUTO,
        .frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS,
        .application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO,
        .complexity = 0,
        .enable_fec = false,
        .enable_dtx = true,
        .enable_vbr = true,
    };
    esp_audio_err_t audio_result = esp_opus_enc_open(&encoder_config, sizeof(encoder_config), &s_encoder);
    if (audio_result != ESP_AUDIO_ERR_OK || !s_encoder) return ESP_FAIL;
    audio_result = esp_opus_enc_get_frame_size(s_encoder, &s_encoder_frame_bytes,
                                                &s_encoder_output_bytes);
    if (audio_result != ESP_AUDIO_ERR_OK || s_encoder_frame_bytes != kPcmSamples * sizeof(int16_t) ||
        s_encoder_output_bytes > kMaxOpusBytes) {
        esp_opus_enc_close(s_encoder);
        s_encoder = nullptr;
        return ESP_ERR_INVALID_SIZE;
    }

    esp_opus_dec_cfg_t decoder_config = {
        .sample_rate = kSampleRate,
        .channel = ESP_AUDIO_MONO,
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS,
        .self_delimited = false,
    };
    audio_result = esp_opus_dec_open(&decoder_config, sizeof(decoder_config), &s_decoder);
    if (audio_result != ESP_AUDIO_ERR_OK || !s_decoder) {
        esp_opus_enc_close(s_encoder);
        s_encoder = nullptr;
        return ESP_FAIL;
    }
    return ESP_OK;
}

void deinitialize_chat();

esp_err_t initialize_chat() {
    if (s_chat && (xEventGroupGetBits(s_events) & kTransportConnected)) return ESP_OK;
    if (s_chat) deinitialize_chat();
    if (!(xEventGroupGetBits(s_events) & kNetworkOnline)) return ESP_ERR_INVALID_STATE;

    esp_xiaozhi_chat_info_t info = {};
    esp_err_t result = esp_xiaozhi_chat_get_info(&info);
    if (result != ESP_OK) {
        (void)esp_xiaozhi_chat_free_info(&info);
        return result;
    }
    if (!info.has_websocket_config) {
        esp_xiaozhi_chat_free_info(&info);
        return ESP_ERR_NOT_FOUND;
    }

    esp_xiaozhi_chat_config_t config = ESP_XIAOZHI_CHAT_DEFAULT_CONFIG();
    config.audio_callback = incoming_audio;
    config.event_callback = protocol_event;
    config.has_websocket_config = true;
    config.has_mqtt_config = false;
    result = esp_xiaozhi_chat_init(&config, &s_chat);
    if (result == ESP_OK) result = esp_xiaozhi_chat_start(s_chat);
    esp_xiaozhi_chat_free_info(&info);
    if (result != ESP_OK) {
        if (s_chat) esp_xiaozhi_chat_deinit(s_chat);
        s_chat = 0;
        return result;
    }
    EventBits_t bits = xEventGroupWaitBits(s_events, kTransportConnected, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(6000));
    return (bits & kTransportConnected) ? ESP_OK : ESP_ERR_TIMEOUT;
}

void deinitialize_chat() {
    s_running_listening = false;
    reset_audio_flow();
    if (!s_chat) return;
    (void)esp_xiaozhi_chat_close_audio_channel(s_chat);
    (void)esp_xiaozhi_chat_stop(s_chat);
    (void)esp_xiaozhi_chat_deinit(s_chat);
    s_chat = 0;
    xEventGroupClearBits(s_events, kTransportConnected);
}

struct HttpBody { char *data; size_t used; bool overflow; };
constexpr size_t kProgressResponseBytes = 6144;
esp_err_t receive_http(esp_http_client_event_t *event) {
    auto *body = static_cast<HttpBody *>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_DATA && body && event->data_len > 0) {
        if (body->used + (size_t)event->data_len >= kProgressResponseBytes) { body->overflow = true; return ESP_FAIL; }
        std::memcpy(body->data + body->used, event->data, event->data_len);
        body->used += event->data_len; body->data[body->used] = 0;
    }
    return ESP_OK;
}
bool json_u32(cJSON *value, uint32_t *out) {
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) || value->valuedouble < 0 ||
        value->valuedouble > UINT32_MAX || std::floor(value->valuedouble) != value->valuedouble) return false;
    *out = (uint32_t)value->valuedouble; return true;
}
const char *json_text(cJSON *object, const char *key) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : nullptr;
}
bool parse_progress(cJSON *root, travel_progress_snapshot_t *snapshot) {
    const char *trip = json_text(root, "trip_id"), *version = json_text(root, "content_version");
    cJSON *activities = cJSON_GetObjectItemCaseSensitive(root, "activities");
    int count = cJSON_GetArraySize(activities);
    if (!trip || std::strcmp(trip, TRAVEL_TRIP_ID) || !version || !*version ||
        std::strlen(version) >= sizeof(snapshot->content_version) || !cJSON_IsArray(activities) ||
        count < 1 || count > (int)TRAVEL_ACTIVITY_MAX ||
        !json_u32(cJSON_GetObjectItemCaseSensitive(root, "progress_revision"), &snapshot->progress_revision)) return false;
    std::strcpy(snapshot->content_version, version); snapshot->count = (uint8_t)count;
    for (int i = 0; i < count; ++i) {
        cJSON *entry = cJSON_GetArrayItem(activities, i);
        const char *id = json_text(entry, "activity_id"), *status = json_text(entry, "status");
        if (!id || !*id || std::strlen(id) >= TRAVEL_ACTIVITY_ID_SIZE || !status ||
            !json_u32(cJSON_GetObjectItemCaseSensitive(entry, "revision"), &snapshot->activities[i].revision)) return false;
        unsigned value;
        for (value = 0; value <= TRAVEL_ACTIVITY_SKIPPED; ++value)
            if (!std::strcmp(status, travel_progress_status_name(value))) break;
        if (value > TRAVEL_ACTIVITY_SKIPPED) return false;
        std::strcpy(snapshot->activities[i].activity_id, id); snapshot->activities[i].status = (uint8_t)value;
    }
    return true;
}
/* One bounded response allocation; cJSON and the body are released before audio starts. */
esp_err_t http_request(const char *url, esp_http_client_method_t method, const char *payload,
                       size_t size, int *http_status, cJSON **response) {
    if (!url || !*url) return ESP_ERR_INVALID_ARG;
    char *buffer = static_cast<char *>(std::calloc(1, kProgressResponseBytes));
    if (!buffer) return ESP_ERR_NO_MEM;
    HttpBody body = {buffer, 0, false};
    esp_http_client_config_t config = {};
    config.url = url; config.timeout_ms = CONFIG_KOALA_STATE_SYNC_TIMEOUT_MS;
    config.crt_bundle_attach = esp_crt_bundle_attach; config.event_handler = receive_http; config.user_data = &body;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { std::free(buffer); return ESP_ERR_NO_MEM; }
    char device_id[24];
    if (!voice_wifi_device_id(device_id, sizeof(device_id))) {
        esp_http_client_cleanup(client); std::free(buffer); return ESP_ERR_INVALID_STATE;
    }
    esp_http_client_set_method(client, method);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Device-Id", device_id);
    if (CONFIG_KOALA_STATE_SYNC_TOKEN[0]) {
        char authorization[192];
        int count = snprintf(authorization, sizeof(authorization), "Bearer %s", CONFIG_KOALA_STATE_SYNC_TOKEN);
        if (count <= 0 || (size_t)count >= sizeof(authorization)) {
            esp_http_client_cleanup(client); std::free(buffer); return ESP_ERR_INVALID_SIZE;
        }
        esp_http_client_set_header(client, "Authorization", authorization);
    }
    if (payload && size) esp_http_client_set_post_field(client, payload, (int)size);
    esp_err_t result = esp_http_client_perform(client);
    *http_status = result == ESP_OK ? esp_http_client_get_status_code(client) : 0;
    if (result == ESP_OK && !body.overflow && body.used)
        *response = cJSON_ParseWithLengthOpts(body.data, body.used + 1, nullptr, true);
    esp_http_client_cleanup(client); std::free(buffer);
    if (body.overflow) return ESP_ERR_INVALID_SIZE;
    return result;
}
bool deliver_progress(cJSON *root, voice_progress_request_kind_t kind,
                      const travel_progress_operation_t *operation = nullptr) {
    if (!s_progress_callback || !root) return false;
    auto *snapshot = static_cast<travel_progress_snapshot_t *>(std::calloc(1, sizeof(travel_progress_snapshot_t)));
    if (!snapshot) return false;
    bool result = parse_progress(root, snapshot);
    if (result) {
        voice_progress_request_t request = {};
        request.kind = kind; request.snapshot = snapshot;
        if (operation) request.operation = *operation;
        result = s_progress_callback(&request, s_progress_context);
    }
    std::free(snapshot); return result;
}
bool state_endpoint_url(const char *endpoint, char *url, size_t capacity) {
    const char *base = CONFIG_KOALA_STATE_SYNC_URL;
    const char *tail = std::strrchr(base, '/');
    if (!tail || std::strcmp(tail, "/device-state")) return false;
    int count = snprintf(url, capacity, "%.*s/%s", (int)(tail - base), base, endpoint);
    return count > 0 && (size_t)count < capacity;
}
bool synchronize_time() {
    if (!(xEventGroupGetBits(s_events) & kNetworkOnline)) return false;
    char url[384];
    if (!state_endpoint_url("time", url, sizeof(url))) return false;
    int status = 0; cJSON *response = nullptr;
    esp_err_t result = http_request(url, HTTP_METHOD_GET, nullptr, 0, &status, &response);
    cJSON *value = response ? cJSON_GetObjectItemCaseSensitive(response, "server_time") : nullptr;
    double seconds = cJSON_IsNumber(value) ? cJSON_GetNumberValue(value) : 0;
    bool valid = result == ESP_OK && status == 200 && std::isfinite(seconds) &&
                 std::floor(seconds) == seconds && seconds >= 1735689600.0 && seconds < 4102444800.0 &&
                 travel_model_server_time_valid((int64_t)seconds);
    if (valid) {
        struct timeval clock = {};
        clock.tv_sec = (time_t)seconds;
        valid = settimeofday(&clock, nullptr) == 0;
    }
    cJSON_Delete(response);
    if (valid) ESP_LOGI(TAG, "Clock synchronized from trip service");
    else ESP_LOGW(TAG, "Trip service clock unavailable; date remains unverified");
    return valid;
}
void synchronize_progress() {
    if (!s_progress_callback || !(xEventGroupGetBits(s_events) & kNetworkOnline)) return;
    char url[384];
    if (!state_endpoint_url("activity-progress", url, sizeof(url))) return;
    /* Bound one flush to the catalogue size. A failed request remains persisted for reconnect. */
    for (unsigned i = 0; i < TRAVEL_ACTIVITY_MAX; ++i) {
        voice_progress_request_t request = {}; request.kind = VOICE_PROGRESS_NEXT;
        if (!s_progress_callback(&request, s_progress_context) || !request.has_operation) break;
        const auto &op = request.operation;
        char payload[384];
        int length = snprintf(payload, sizeof(payload),
            "{\"operation_id\":\"%s\",\"activity_id\":\"%s\",\"status\":\"%s\","
            "\"content_version\":\"%s\",\"expected_activity_revision\":%lu}",
            op.operation_id, op.activity_id, travel_progress_status_name(op.status), op.content_version,
            (unsigned long)op.expected_activity_revision);
        if (length <= 0 || (size_t)length >= sizeof(payload)) break;
        int status = 0; cJSON *response = nullptr;
        esp_err_t result = http_request(url, HTTP_METHOD_POST, payload, length, &status, &response);
        bool applied = false;
        if (result == ESP_OK && response) {
            cJSON *snapshot = cJSON_GetObjectItemCaseSensitive(response, "progress");
            const char *error = json_text(response, "error");
            const char *receipt = json_text(response, "result");
            bool accepted_receipt = receipt && (!std::strcmp(receipt, "applied") ||
                !std::strcmp(receipt, "unchanged") || !std::strcmp(receipt, "duplicate"));
            if (status == 200 && accepted_receipt && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response, "ok")))
                applied = deliver_progress(snapshot, VOICE_PROGRESS_ACK, &op);
            else if (status == 409 && error && !std::strcmp(error, "activity_revision_conflict"))
                applied = deliver_progress(snapshot, VOICE_PROGRESS_CONFLICT, &op);
            else if (status == 409 && error && !std::strcmp(error, "content_version_mismatch"))
                (void)deliver_progress(snapshot, VOICE_PROGRESS_PAUSE, &op);
        }
        cJSON_Delete(response);
        if (!applied) break;
    }
    int status = 0; cJSON *response = nullptr;
    if (http_request(url, HTTP_METHOD_GET, nullptr, 0, &status, &response) == ESP_OK && status == 200)
        (void)deliver_progress(response, VOICE_PROGRESS_SNAPSHOT);
    cJSON_Delete(response);
}
esp_err_t upload_state(const char *payload, size_t size) {
    if (!payload || !size) return ESP_ERR_INVALID_ARG;
    int status = 0; cJSON *response = nullptr;
    esp_err_t result = http_request(CONFIG_KOALA_STATE_SYNC_URL, HTTP_METHOD_PUT, payload, size, &status, &response);
    if (result == ESP_OK && status >= 200 && status < 300 && response)
        (void)deliver_progress(cJSON_GetObjectItemCaseSensitive(response, "progress"), VOICE_PROGRESS_SNAPSHOT);
    cJSON_Delete(response);
    return result != ESP_OK ? result : status >= 200 && status < 300 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t start_turn(const Command &command) {
    s_running_listening = false;
    publish(VOICE_ASSISTANT_SYNCING);
    xEventGroupSetBits(s_events, kDropPlayback);
    xQueueReset(s_rx_packets);
    if (s_chat && (xEventGroupGetBits(s_events) & kChannelReady)) {
        (void)esp_xiaozhi_chat_send_abort_speaking(
            s_chat, ESP_XIAOZHI_CHAT_ABORT_SPEAKING_REASON_STOP_LISTENING);
    }

    synchronize_progress();
    esp_err_t result = upload_state(command.payload, command.payload_size);
    if (result == ESP_OK) result = initialize_chat();
    if (result != ESP_OK) return result;

    if (!(xEventGroupGetBits(s_events) & kChannelReady)) {
        esp_xiaozhi_chat_audio_t audio = {};
        audio.format = "opus";
        audio.sample_rate = kSampleRate;
        audio.channels = 1;
        audio.frame_duration = kFrameDurationMs;
        result = esp_xiaozhi_chat_open_audio_channel(s_chat, &audio, nullptr, 0);
        if (result != ESP_OK) return result;
        EventBits_t bits = xEventGroupWaitBits(s_events, kChannelReady, pdFALSE, pdTRUE,
                                               pdMS_TO_TICKS(3000));
        if (!(bits & kChannelReady)) return ESP_ERR_TIMEOUT;
    }
    result = esp_xiaozhi_chat_send_start_listening(
        s_chat, ESP_XIAOZHI_CHAT_LISTENING_MODE_MANUAL);
    if (result == ESP_OK) {
        s_running_listening = true;
        xEventGroupClearBits(s_events, kDropPlayback | kSpeaking);
        xEventGroupSetBits(s_events, kSendAllowed);
        publish(VOICE_ASSISTANT_LISTENING);
    }
    return result;
}

void stop_turn() {
    /* The release handler already stopped that capture. A newer button press may now be
     * recording its next turn: an older queued Stop must never clear that capture flag. */
    if (!s_chat || !s_running_listening) return;
    for (int attempt = 0; attempt < 12 && s_requested_turn.load() == s_running_turn &&
         uxQueueMessagesWaiting(s_tx_packets); ++attempt)
        vTaskDelay(pdMS_TO_TICKS(50));
    (void)esp_xiaozhi_chat_send_stop_listening(s_chat);
    s_running_listening = false;
    xEventGroupClearBits(s_events, kSendAllowed);
    publish(VOICE_ASSISTANT_THINKING);
}

void complete_pending_stop() {
    uint32_t pending = s_stop_fallback.load();
    if (!pending) return;
    if (pending == s_running_turn) {
        stop_turn();
        (void)s_stop_fallback.compare_exchange_strong(pending, 0);
    } else if (s_running_turn && (int32_t)(s_running_turn - pending) > 0) {
        /* A newer Start already aborted the old turn. Do not let an old fallback stop it. */
        (void)s_stop_fallback.compare_exchange_strong(pending, 0);
    }
    /* A stop for a Start still in the command queue waits until that Start is processed. */
}
void control_task(void *) {
    Command command;
    TickType_t last_clock_attempt = 0;
    bool clock_attempted = false;
    for (;;) {
        complete_pending_stop();
        if (xQueueReceive(s_commands, &command, pdMS_TO_TICKS(100)) != pdTRUE) {
            TickType_t now = xTaskGetTickCount();
            if ((xEventGroupGetBits(s_events) & kNetworkOnline) &&
                !travel_model_clock_valid(time(nullptr)) &&
                (!clock_attempted || now - last_clock_attempt >= pdMS_TO_TICKS(60000))) {
                last_clock_attempt = now; clock_attempted = true;
                (void)synchronize_time();
            }
            if (xEventGroupGetBits(s_events) & kProgressRefresh) {
                xEventGroupClearBits(s_events, kProgressRefresh);
                synchronize_progress();
            }
            continue;
        }
        switch (command.type) {
        case CommandType::NetworkUp:
            last_clock_attempt = xTaskGetTickCount(); clock_attempted = true;
            (void)synchronize_time();
            synchronize_progress();
            if (!s_chat) {
                esp_err_t result = initialize_chat();
                if (result != ESP_OK) ESP_LOGW(TAG, "Voice server pre-connect failed: %s", esp_err_to_name(result));
            }
            publish(s_chat ? VOICE_ASSISTANT_READY : VOICE_ASSISTANT_ERROR);
            break;
        case CommandType::NetworkDown:
            deinitialize_chat();
            publish(VOICE_ASSISTANT_OFFLINE);
            break;
        case CommandType::Start: {
            s_running_turn = command.turn;
            esp_err_t result = start_turn(command);
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "Unable to start voice turn: %s", esp_err_to_name(result));
                deinitialize_chat();
                publish(VOICE_ASSISTANT_ERROR);
                xEventGroupSetBits(s_events, kProgressRefresh);
            }
            break;
        }
        case CommandType::Stop:
            if (command.turn == s_running_turn) stop_turn();
            break;
        case CommandType::Suspend:
            deinitialize_chat();
            xSemaphoreGive(s_suspend_done);
            break;
        }
    }
}

void capture_task(void *) {
    int16_t pcm[kPcmSamples];
    uint8_t encoded[kMaxOpusBytes];
    for (;;) {
        if (!(xEventGroupGetBits(s_events) & kCapturing)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (bsp_audio_read(pcm, sizeof(pcm)) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        esp_audio_enc_in_frame_t input = {
            .buffer = reinterpret_cast<uint8_t *>(pcm), .len = sizeof(pcm),
        };
        esp_audio_enc_out_frame_t output = {};
        output.buffer = encoded;
        output.len = sizeof(encoded);
        if (esp_opus_enc_process(s_encoder, &input, &output) != ESP_AUDIO_ERR_OK ||
            output.encoded_bytes == 0 || output.encoded_bytes > kMaxOpusBytes) continue;
        OpusPacket packet = {};
        packet.size = (uint16_t)output.encoded_bytes;
        std::memcpy(packet.data, encoded, packet.size);
        if (xQueueSend(s_tx_packets, &packet, 0) != pdTRUE) {
            OpusPacket discarded;
            (void)xQueueReceive(s_tx_packets, &discarded, 0);
            (void)xQueueSend(s_tx_packets, &packet, 0);
        }
    }
}

void sender_task(void *) {
    OpusPacket packet;
    for (;;) {
        if (!(xEventGroupGetBits(s_events) & kSendAllowed)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (xQueueReceive(s_tx_packets, &packet, pdMS_TO_TICKS(60)) == pdTRUE && s_chat) {
            esp_err_t result = esp_xiaozhi_chat_send_audio_data(
                s_chat, reinterpret_cast<const char *>(packet.data), packet.size);
            if (result != ESP_OK) ESP_LOGW(TAG, "Audio send failed: %s", esp_err_to_name(result));
        }
    }
}

void playback_task(void *) {
    OpusPacket packet;
    int16_t pcm[kPcmSamples];
    for (;;) {
        if (xQueueReceive(s_rx_packets, &packet, portMAX_DELAY) != pdTRUE) continue;
        if (xEventGroupGetBits(s_events) & kDropPlayback) continue;
        esp_audio_dec_in_raw_t input = {
            .buffer = packet.data, .len = packet.size, .consumed = 0,
            .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
        };
        esp_audio_dec_out_frame_t output = {};
        output.buffer = reinterpret_cast<uint8_t *>(pcm);
        output.len = sizeof(pcm);
        esp_audio_dec_info_t info = {};
        if (esp_opus_dec_decode(s_decoder, &input, &output, &info) == ESP_AUDIO_ERR_OK &&
            output.decoded_size && !(xEventGroupGetBits(s_events) & kDropPlayback)) {
            (void)bsp_audio_write(pcm, output.decoded_size);
        }
    }
}

bool enqueue(CommandType type, const char *payload = nullptr, size_t payload_size = 0, uint32_t turn = 0) {
    if (!s_commands || payload_size >= kStatePayloadBytes) return false;
    Command command = {};
    command.type = type;
    command.turn = turn;
    command.payload_size = (uint16_t)payload_size;
    if (payload && payload_size) std::memcpy(command.payload, payload, payload_size);
    return xQueueSend(s_commands, &command, 0) == pdTRUE;
}

void cleanup_initialization() {
    if (s_playback_task) { vTaskDelete(s_playback_task); s_playback_task = nullptr; }
    if (s_sender_task) { vTaskDelete(s_sender_task); s_sender_task = nullptr; }
    if (s_capture_task) { vTaskDelete(s_capture_task); s_capture_task = nullptr; }
    if (s_control_task) { vTaskDelete(s_control_task); s_control_task = nullptr; }
    if (s_event_handler_registered) {
        (void)esp_event_handler_unregister(ESP_XIAOZHI_CHAT_EVENTS, ESP_EVENT_ANY_ID,
                                           system_event);
        s_event_handler_registered = false;
    }
    if (s_decoder) { (void)esp_opus_dec_close(s_decoder); s_decoder = nullptr; }
    if (s_encoder) { esp_opus_enc_close(s_encoder); s_encoder = nullptr; }
    (void)bsp_audio_sleep();
    if (s_suspend_done) { vSemaphoreDelete(s_suspend_done); s_suspend_done = nullptr; }
    if (s_rx_packets) { vQueueDelete(s_rx_packets); s_rx_packets = nullptr; }
    if (s_tx_packets) { vQueueDelete(s_tx_packets); s_tx_packets = nullptr; }
    if (s_commands) { vQueueDelete(s_commands); s_commands = nullptr; }
    if (s_events) { vEventGroupDelete(s_events); s_events = nullptr; }
}

}  // namespace

extern "C" esp_err_t voice_assistant_init(voice_assistant_status_callback_t callback,
                                             void *context) {
    if (s_events) return ESP_OK;
    s_status_callback = callback;
    s_status_context = context;
    s_events = xEventGroupCreate();
    s_commands = xQueueCreate(4, sizeof(Command));
    // Keep enough pre-roll to preserve the start of speech while the state PUT
    // and WebSocket handshake finish, without consuming most of the C3 heap.
    s_tx_packets = xQueueCreate(24, sizeof(OpusPacket));
    s_rx_packets = xQueueCreate(8, sizeof(OpusPacket));
    s_suspend_done = xSemaphoreCreateBinary();
    if (!s_events || !s_commands || !s_tx_packets || !s_rx_packets || !s_suspend_done) {
        cleanup_initialization();
        return ESP_ERR_NO_MEM;
    }
    xEventGroupSetBits(s_events, kDropPlayback);

    esp_err_t result = bsp_audio_init();
    if (result == ESP_OK) result = bsp_audio_set_format(kSampleRate, 16, 1);
    if (result == ESP_OK) result = initialize_codecs();
    if (result != ESP_OK) {
        cleanup_initialization();
        return result;
    }
    bsp_audio_set_volume(70);
    result = esp_event_handler_register(ESP_XIAOZHI_CHAT_EVENTS, ESP_EVENT_ANY_ID,
                                        system_event, nullptr);
    if (result != ESP_OK) {
        cleanup_initialization();
        return result;
    }
    s_event_handler_registered = true;

    if (xTaskCreate(control_task, "voice_ctrl", 7168, nullptr, 5, &s_control_task) != pdPASS ||
        xTaskCreate(capture_task, "voice_mic", 5120, nullptr, 6, &s_capture_task) != pdPASS ||
        xTaskCreate(sender_task, "voice_send", 3584, nullptr, 6, &s_sender_task) != pdPASS ||
        xTaskCreate(playback_task, "voice_play", 4608, nullptr, 6, &s_playback_task) != pdPASS) {
        cleanup_initialization();
        return ESP_ERR_NO_MEM;
    }
    publish(VOICE_ASSISTANT_OFFLINE);
    return ESP_OK;
}

extern "C" void voice_assistant_set_network(bool online) {
    if (!s_events) return;
    bool was_online = (xEventGroupGetBits(s_events) & kNetworkOnline) != 0;
    if (was_online == online) return;
    if (online) xEventGroupSetBits(s_events, kNetworkOnline);
    else xEventGroupClearBits(s_events, kNetworkOnline);
    (void)enqueue(online ? CommandType::NetworkUp : CommandType::NetworkDown);
}

extern "C" bool voice_assistant_begin(const voice_state_snapshot_t *snapshot) {
    if (!s_events || !snapshot || !(xEventGroupGetBits(s_events) & kNetworkOnline)) {
        publish(VOICE_ASSISTANT_OFFLINE);
        return false;
    }
    if (xEventGroupGetBits(s_events) & kCapturing) return false;
    char payload[kStatePayloadBytes];
    size_t payload_size = voice_state_payload_build(snapshot, payload, sizeof(payload));
    if (!payload_size) return false;

    xQueueReset(s_tx_packets);
    xQueueReset(s_rx_packets);
    xEventGroupSetBits(s_events, kCapturing | kDropPlayback);
    xEventGroupClearBits(s_events, kSendAllowed | kSpeaking);
    uint32_t turn = s_requested_turn.fetch_add(1) + 1;
    if (!turn) turn = s_requested_turn.fetch_add(1) + 1;
    if (!enqueue(CommandType::Start, payload, payload_size, turn)) {
        xEventGroupClearBits(s_events, kCapturing);
        return false;
    }
    publish(VOICE_ASSISTANT_SYNCING);
    return true;
}

extern "C" void voice_assistant_end(void) {
    if (!s_events || !(xEventGroupGetBits(s_events) & kCapturing)) return;
    xEventGroupClearBits(s_events, kCapturing);
    uint32_t turn = s_requested_turn.load();
    if (!enqueue(CommandType::Stop, nullptr, 0, turn)) s_stop_fallback.store(turn);
}

extern "C" void voice_assistant_prepare_sleep(void) {
    if (!s_events) return;
    reset_audio_flow();
    while (xSemaphoreTake(s_suspend_done, 0) == pdTRUE) {}
    if (enqueue(CommandType::Suspend))
        (void)xSemaphoreTake(s_suspend_done, pdMS_TO_TICKS(2500));
}

extern "C" voice_assistant_status_t voice_assistant_get_status(void) {
    return s_status;
}

extern "C" void voice_assistant_set_progress_callback(voice_progress_callback_t callback, void *context) {
    s_progress_callback = callback; s_progress_context = context;
}
extern "C" void voice_assistant_refresh_progress(void) {
    if (s_events) xEventGroupSetBits(s_events, kProgressRefresh);
}
