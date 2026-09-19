/* Application-owned Wi-Fi SoftAP, DNS Captive Portal, and HTTP Web Sync service. */
#include "travel_wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <sys/time.h>

static const char *TAG = "travel_wifi";
static bool s_initialized = false;
static bool s_ap_active = false;
static esp_netif_t *s_netif_ap = NULL;
static httpd_handle_t s_httpd = NULL;
static TaskHandle_t s_dns_task = NULL;
static int s_dns_socket = -1;
static travel_wifi_sync_cb_t s_sync_cb = NULL;
static travel_custom_schedule_t s_current_trip = {0};
static travel_net_status_t s_status = {TRAVEL_NET_OFFLINE, false, false};
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static const char s_index_html[] =
"<!DOCTYPE html>"
"<html lang=\"zh-CN\">"
"<head>"
"<meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
"<title>考拉旅行伴侣</title>"
"<style>"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;margin:0;padding:16px;background:#f5f6fa;color:#2c3e50;}"
".card{max-width:440px;margin:0 auto 16px;background:#fff;border-radius:14px;padding:20px;box-shadow:0 4px 12px rgba(0,0,0,0.06);}"
"h1{font-size:20px;margin:0 0 6px;color:#123565;display:flex;align-items:center;gap:8px;}"
"p{font-size:13px;color:#7f8c8d;margin:0 0 16px;line-height:1.4;}"
".sync-tip{font-size:12px;color:#27ae60;margin-bottom:12px;font-weight:500;}"
"label{display:block;font-size:13px;font-weight:600;margin:12px 0 4px;color:#34495e;}"
"input,textarea{width:100%;box-sizing:border-box;padding:10px 12px;border:1px solid #dcdde1;border-radius:8px;font-size:14px;outline:none;}"
"input:focus,textarea:focus{border-color:#123565;}"
"button{width:100%;padding:12px;margin-top:16px;background:#123565;color:#fff;border:none;border-radius:8px;font-size:15px;font-weight:600;cursor:pointer;}"
"button:hover{background:#0e294f;}"
"button:disabled{background:#95a5a6;cursor:not-allowed;}"
".sec-title{font-size:14px;font-weight:bold;margin:16px 0 8px;cursor:pointer;color:#57606f;}"
".hidden{display:none;}"
".success-msg{background:#e8f5e9;color:#2e7d32;padding:12px;border-radius:8px;margin-top:14px;font-weight:bold;font-size:14px;text-align:center;}"
"</style>"
"</head>"
"<body>"
"<div class=\"card\">"
"<h1>🐨 考拉旅行伴侣</h1>"
"<p>已连接到考拉同步模式。你可以在此修改目的地与探索备忘。</p>"
"<div id=\"syncTip\" class=\"sync-tip\">⏳ 正在同步设备时间...</div>"
"<label for=\"dest\">目的地名称</label>"
"<input id=\"dest\" placeholder=\"例如：上海 / 西湖 / 迪士尼\" value=\"上海\">"
"<label for=\"t1\">探索任务 1</label>"
"<input id=\"t1\" placeholder=\"例如：找一艘船\" value=\"找一艘船\">"
"<label for=\"t2\">探索任务 2</label>"
"<input id=\"t2\" placeholder=\"例如：找一片叶\" value=\"找一片叶\">"
"<label for=\"t3\">探索任务 3</label>"
"<input id=\"t3\" placeholder=\"例如：听一听声音\" value=\"听一听声音\">"
"<button id=\"btnSave\" onclick=\"saveTrip()\">✔ 保存并同步到考拉</button>"
"<div id=\"statusBox\"></div>"
"<div class=\"sec-title\" onclick=\"toggleBatch()\">⚙ 电脑/批量 JSON 模式 ▾</div>"
"<div id=\"batchSec\" class=\"hidden\">"
"<textarea id=\"jsonArea\" rows=\"6\"></textarea>"
"<button style=\"background:#57606f;margin-top:8px;\" onclick=\"applyJson()\">导入 JSON 行程</button>"
"</div>"
"</div>"
"<script>"
"window.addEventListener('DOMContentLoaded',()=>{"
"  syncTime();"
"  loadCurrent();"
"});"
"function syncTime(){"
"  fetch('/api/time',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({epoch:Math.floor(Date.now()/1000)})})"
"  .then(r=>r.json()).then(()=>{document.getElementById('syncTip').innerText='✔ 设备时间已自动校准';})"
"  .catch(()=>{document.getElementById('syncTip').innerText='✔ 离线模式就绪';});"
"}"
"function loadCurrent(){"
"  fetch('/api/trip').then(r=>r.json()).then(d=>{"
"    if(d.title)document.getElementById('dest').value=d.title;"
"    if(d.tasks&&d.tasks[0])document.getElementById('t1').value=d.tasks[0];"
"    if(d.tasks&&d.tasks[1])document.getElementById('t2').value=d.tasks[1];"
"    if(d.tasks&&d.tasks[2])document.getElementById('t3').value=d.tasks[2];"
"    document.getElementById('jsonArea').value=JSON.stringify(d,null,2);"
"  }).catch(()=>{});"
"}"
"function toggleBatch(){"
"  const s=document.getElementById('batchSec');"
"  s.classList.toggle('hidden');"
"}"
"function saveTrip(){"
"  const title=document.getElementById('dest').value.trim();"
"  const tasks=[document.getElementById('t1').value.trim(),document.getElementById('t2').value.trim(),document.getElementById('t3').value.trim()].filter(t=>t.length>0);"
"  if(!title){alert('请输入目的地名称');return;}"
"  send({epoch:Math.floor(Date.now()/1000),title:title,tasks:tasks});"
"}"
"function applyJson(){"
"  try{"
"    const d=JSON.parse(document.getElementById('jsonArea').value);"
"    if(!d.title||!Array.isArray(d.tasks)){alert('JSON 必须包含 title 和 tasks 数组');return;}"
"    d.epoch=Math.floor(Date.now()/1000);"
"    send(d);"
"  }catch(e){alert('JSON 格式错误: '+e.message);}"
"}"
"function send(payload){"
"  const btn=document.getElementById('btnSave');"
"  btn.disabled=true;btn.innerText='正在同步...';"
"  fetch('/api/save_trip',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)})"
"  .then(r=>r.json()).then(d=>{"
"    btn.innerText='✔ 已同步';"
"    document.getElementById('statusBox').innerHTML='<div class=\"success-msg\">✔ 同步成功！考拉屏幕已更新，你可以断开连接了。</div>';"
"  }).catch(e=>{"
"    alert('保存失败，请检查网络');"
"    btn.disabled=false;btn.innerText='✔ 保存并同步到考拉';"
"  });"
"}"
"</script>"
"</body>"
"</html>";

travel_net_status_t travel_wifi_status(void) {
    portENTER_CRITICAL(&s_lock);
    travel_net_status_t status = s_status;
    portEXIT_CRITICAL(&s_lock);
    return status;
}

bool travel_wifi_is_softap_active(void) {
    return s_ap_active;
}

void travel_wifi_set_current_trip(const travel_custom_schedule_t *schedule) {
    if (schedule) {
        memcpy(&s_current_trip, schedule, sizeof(s_current_trip));
    }
}

static void dns_server_task(void *pvParameters) {
    (void)pvParameters;
    uint8_t rx_buffer[256];
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    s_dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_dns_socket < 0) {
        ESP_LOGE(TAG, "DNS socket create failed");
        vTaskDelete(NULL);
        return;
    }
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(s_dns_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (bind(s_dns_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "DNS socket bind failed");
        close(s_dns_socket);
        s_dns_socket = -1;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "DNS captive portal server started on port 53");
    while (s_ap_active) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int len = recvfrom(s_dns_socket, rx_buffer, sizeof(rx_buffer), 0,
                           (struct sockaddr *)&client_addr, &client_len);
        if (len < 12) continue;

        rx_buffer[2] = 0x81;
        rx_buffer[3] = 0x80;
        rx_buffer[4] = 0x00; rx_buffer[5] = 0x01;
        rx_buffer[6] = 0x00; rx_buffer[7] = 0x01;
        rx_buffer[8] = 0x00; rx_buffer[9] = 0x00;
        rx_buffer[10] = 0x00; rx_buffer[11] = 0x00;

        int idx = 12;
        while (idx < len && rx_buffer[idx] != 0) {
            idx += rx_buffer[idx] + 1;
        }
        idx += 5;
        if (idx + 16 > (int)sizeof(rx_buffer)) continue;

        rx_buffer[idx++] = 0xC0;
        rx_buffer[idx++] = 0x0C;
        rx_buffer[idx++] = 0x00;
        rx_buffer[idx++] = 0x01;
        rx_buffer[idx++] = 0x00;
        rx_buffer[idx++] = 0x01;
        rx_buffer[idx++] = 0x00;
        rx_buffer[idx++] = 0x00;
        rx_buffer[idx++] = 0x00;
        rx_buffer[idx++] = 0x3C;
        rx_buffer[idx++] = 0x00;
        rx_buffer[idx++] = 0x04;
        rx_buffer[idx++] = 192;
        rx_buffer[idx++] = 168;
        rx_buffer[idx++] = 4;
        rx_buffer[idx++] = 1;

        sendto(s_dns_socket, rx_buffer, idx, 0, (struct sockaddr *)&client_addr, client_len);
    }
    if (s_dns_socket >= 0) {
        close(s_dns_socket);
        s_dns_socket = -1;
    }
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t http_root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, s_index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t http_captive_redirect(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t http_get_trip_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "title", s_current_trip.title[0] ? s_current_trip.title : "上海");
    cJSON *arr = cJSON_AddArrayToObject(root, "tasks");
    size_t count = s_current_trip.task_count ? s_current_trip.task_count : 3;
    for (size_t i = 0; i < count; ++i) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(s_current_trip.tasks[i]));
    }
    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t http_post_time_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret > 0) {
        buf[ret] = '\0';
        cJSON *root = cJSON_Parse(buf);
        if (root) {
            cJSON *ep = cJSON_GetObjectItem(root, "epoch");
            if (cJSON_IsNumber(ep) && ep->valuedouble > 1700000000.0) {
                struct timeval tv = { .tv_sec = (time_t)ep->valuedouble, .tv_usec = 0 };
                settimeofday(&tv, NULL);
                ESP_LOGI(TAG, "Clock calibrated via web sync: epoch=%ld", (long)tv.tv_sec);
            }
            cJSON_Delete(root);
        }
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t http_post_save_trip_handler(httpd_req_t *req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret > 0) {
        buf[ret] = '\0';
        cJSON *root = cJSON_Parse(buf);
        if (root) {
            cJSON *ep = cJSON_GetObjectItem(root, "epoch");
            time_t epoch = 0;
            cJSON *title = cJSON_GetObjectItem(root, "title");
            cJSON *tasks = cJSON_GetObjectItem(root, "tasks");
            if (cJSON_IsNumber(ep) && ep->valuedouble > 1700000000.0) {
                struct timeval tv;
                epoch = (time_t)ep->valuedouble;
                tv.tv_sec = epoch;
                tv.tv_usec = 0;
                settimeofday(&tv, NULL);
            }
            if (cJSON_IsString(title) && cJSON_IsArray(tasks)) {
                travel_custom_schedule_t sched;
                int cnt = cJSON_GetArraySize(tasks);
                int i;
                memset(&sched, 0, sizeof(sched));
                strncpy(sched.title, title->valuestring, sizeof(sched.title) - 1);
                if (cnt > TRAVEL_MAX_TASKS) cnt = TRAVEL_MAX_TASKS;
                for (i = 0; i < cnt; ++i) {
                    cJSON *t = cJSON_GetArrayItem(tasks, i);
                    if (cJSON_IsString(t)) {
                        strncpy(sched.tasks[i], t->valuestring, sizeof(sched.tasks[i]) - 1);
                    }
                }
                sched.task_count = (uint8_t)cnt;
                sched.is_custom = true;
                memcpy(&s_current_trip, &sched, sizeof(sched));
                if (s_sync_cb) {
                    s_sync_cb(epoch, &sched);
                }
            }
            cJSON_Delete(root);
        }
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void register_http_handlers(httpd_handle_t server) {
    static const httpd_uri_t handlers[] = {
        { .uri = "/", .method = HTTP_GET, .handler = http_root_handler, .user_ctx = NULL },
        { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = http_root_handler, .user_ctx = NULL },
        { .uri = "/generate_204", .method = HTTP_GET, .handler = http_captive_redirect, .user_ctx = NULL },
        { .uri = "/gen_204", .method = HTTP_GET, .handler = http_captive_redirect, .user_ctx = NULL },
        { .uri = "/ncsi.txt", .method = HTTP_GET, .handler = http_captive_redirect, .user_ctx = NULL },
        { .uri = "/connecttest.txt", .method = HTTP_GET, .handler = http_captive_redirect, .user_ctx = NULL },
        { .uri = "/canonical.html", .method = HTTP_GET, .handler = http_captive_redirect, .user_ctx = NULL },
        { .uri = "/api/trip", .method = HTTP_GET, .handler = http_get_trip_handler, .user_ctx = NULL },
        { .uri = "/api/time", .method = HTTP_POST, .handler = http_post_time_handler, .user_ctx = NULL },
        { .uri = "/api/save_trip", .method = HTTP_POST, .handler = http_post_save_trip_handler, .user_ctx = NULL },
    };
    size_t i;
    for (i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        httpd_register_uri_handler(server, &handlers[i]);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data) {
    (void)arg; (void)event_data;
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            ESP_LOGI(TAG, "Station connected to Koala-Travel");
            portENTER_CRITICAL(&s_lock);
            s_status.state = TRAVEL_NET_CONNECTED;
            portEXIT_CRITICAL(&s_lock);
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            ESP_LOGI(TAG, "Station disconnected from Koala-Travel");
            portENTER_CRITICAL(&s_lock);
            s_status.state = TRAVEL_NET_CONNECTING;
            portEXIT_CRITICAL(&s_lock);
        }
    }
}

esp_err_t travel_wifi_init(travel_wifi_sync_cb_t on_sync) {
    s_sync_cb = on_sync;
    if (s_initialized) return ESP_OK;

    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t loop_err = esp_event_loop_create_default();
    if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE) {
        return loop_err;
    }
    s_netif_ap = esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    s_initialized = true;
    return ESP_OK;
}

esp_err_t travel_wifi_start_softap(void) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_ap_active) return ESP_OK;

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "Koala-Travel",
            .ssid_len = strlen("Koala-Travel"),
            .channel = 1,
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_ap_active = true;
    portENTER_CRITICAL(&s_lock);
    s_status.state = TRAVEL_NET_CONNECTING;
    s_status.provisioning = true;
    portEXIT_CRITICAL(&s_lock);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 4;
    config.stack_size = 4096;
    if (httpd_start(&s_httpd, &config) == ESP_OK) {
        register_http_handlers(s_httpd);
        ESP_LOGI(TAG, "HTTP web server started");
    }

    xTaskCreate(dns_server_task, "dns_task", 3072, NULL, 5, &s_dns_task);
    ESP_LOGI(TAG, "SoftAP 'Koala-Travel' ready at 192.168.4.1");
    return ESP_OK;
}

esp_err_t travel_wifi_stop_softap(void) {
    if (!s_ap_active) return ESP_OK;
    s_ap_active = false;

    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    if (s_dns_socket >= 0) {
        close(s_dns_socket);
        s_dns_socket = -1;
    }
    esp_wifi_stop();

    portENTER_CRITICAL(&s_lock);
    s_status.state = TRAVEL_NET_OFFLINE;
    s_status.provisioning = false;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "SoftAP stopped");
    return ESP_OK;
}
