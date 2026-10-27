#include "network_provision.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define AP_NAME                  "ESP32-Device"
/* Use a unique password per device before deploying real devices. */
#define AP_PASSWORD              "Admin123"

#define EVENT_WIFI_CONNECTED     BIT0
#define EVENT_WIFI_DISCONNECTED  BIT1
#define EVENT_STA_CONFIGURED     BIT2

#define WIFI_CONFIG_QUEUE_LENGTH 1
#define WIFI_FORM_BUFFER_SIZE    384

static const char *TAG = "Wi-Fi";

static EventGroupHandle_t wifi_event_group;
static QueueHandle_t wifi_config_queue;
static httpd_handle_t http_server;

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data);
static void ip_event_handler(void *arg,
                             esp_event_base_t event_base,
                             int32_t event_id,
                             void *event_data);
static esp_err_t root_get_handler(httpd_req_t *req);
static esp_err_t wifi_post_handler(httpd_req_t *req);
static bool url_decode(char *text);
static void apply_station_config(wifi_config_t *config);

bool network_is_connected(void)
{
    if (wifi_event_group == NULL) {
        return false;
    }

    return (xEventGroupGetBits(wifi_event_group) &
            EVENT_WIFI_CONNECTED) != 0;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decode a URL-encoded form field in-place. */
static bool url_decode(char *text)
{
    char *read_ptr = text;
    char *write_ptr = text;

    while (*read_ptr != '\0') {
        if (*read_ptr == '+') {
            *write_ptr++ = ' ';
            read_ptr++;
        } else if (*read_ptr == '%') {
            if (read_ptr[1] == '\0' || read_ptr[2] == '\0') {
                return false;
            }

            int hi = hex_digit(read_ptr[1]);
            int lo = hex_digit(read_ptr[2]);
            if (hi < 0 || lo < 0) {
                return false;
            }

            char decoded = (char)((hi << 4) | lo);
            /* Avoid embedded NUL bytes truncating credential strings. */
            if (decoded == '\0') {
                return false;
            }
            
            *write_ptr++ = decoded;
            read_ptr += 3;
        } else {
            *write_ptr++ = *read_ptr++;
        }
    }

    *write_ptr = '\0';
    return true;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    static const char html[] =
        "<!doctype html>"
        "<html><head><meta name='viewport' "
        "content='width=device-width,initial-scale=1'>"
        "<title>ESP32 Wi-Fi Setup</title></head>"
        "<body style='font-family:sans-serif;max-width:420px;"
        "margin:40px auto;padding:16px'>"
        "<h2>ESP32 SmartFan</h2>"
        "<p>Enter the Wi-Fi network credentials for the ESP32.</p>"
        "<form method='POST' action='/wifi'>"
        "<label>Wi-Fi name (SSID)</label><br>"
        "<input name='ssid' maxlength='32' required "
        "style='width:100%;padding:10px;box-sizing:border-box'>"
        "<br><br>"
        "<label>Wi-Fi password</label><br>"
        "<input name='password' type='password' maxlength='63' "
        "style='width:100%;padding:10px;box-sizing:border-box'>"
        "<br><br><button type='submit'>Save Wi-Fi settings</button>"
        "</form></body></html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t wifi_post_handler(httpd_req_t *req)
{
    char body[WIFI_FORM_BUFFER_SIZE] = {0};
    char ssid[33] = {0};
    char password[64] = {0};
    bool got_ssid = false;

    if (req->content_len == 0 ||
        req->content_len >= sizeof(body)) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Invalid form size.");
    }

    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req,
                                 body + received,
                                 req->content_len - received);
        if (ret <= 0) {
            httpd_resp_set_status(req, "400 Bad Request");
            return httpd_resp_sendstr(req, "Could not read form data.");
        }
        received += (size_t)ret;
    }
    body[received] = '\0';

    char *saveptr = NULL;
    for (char *field = strtok_r(body, "&", &saveptr);
         field != NULL;
         field = strtok_r(NULL, "&", &saveptr)) {
        char *equals = strchr(field, '=');
        if (equals == NULL) {
            continue;
        }

        *equals = '\0';
        char *value = equals + 1;

        if (!url_decode(field) || !url_decode(value)) {
            httpd_resp_set_status(req, "400 Bad Request");
            return httpd_resp_sendstr(req, "Invalid form encoding.");
        }
        
        if (strcmp(field, "ssid") == 0) {
            size_t len = strlen(value);
            if (len == 0 || len > 32) {
                httpd_resp_set_status(req, "400 Bad Request");
                return httpd_resp_sendstr(req, "SSID must be 1 to 32 bytes.");
            }
            memcpy(ssid, value, len);
            got_ssid = true;
        } else if (strcmp(field, "password") == 0) {
            size_t len = strlen(value);
            if (len > 63 || (len > 0 && len < 8)) {
                httpd_resp_set_status(req, "400 Bad Request");
                return httpd_resp_sendstr(
                    req,
                    "Password must be empty for an open network, "
                    "or 8 to 63 characters for a password-protected network."
                );
            }
            memcpy(password, value, len);
        }
    }
    
    if (!got_ssid) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "SSID is required.");
    }
    
    wifi_config_t station_config = {0};
    memcpy(station_config.sta.ssid, ssid, strlen(ssid));
    memcpy(station_config.sta.password, password, strlen(password));

    /* Queue the request so Wi-Fi changes happen in the network task. */
    if (wifi_config_queue == NULL ||
        xQueueOverwrite(wifi_config_queue, &station_config) != pdPASS) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Wi-Fi configuration is unavailable.");
    }
    
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(
        req,
        "<!doctype html><html><body style='font-family:sans-serif'>"
        "<h2>Wi-Fi settings received</h2>"
        "<p>The ESP32 will attempt to connect to this network. "
        "The ESP32-Setup access point remains available.</p>"
        "<a href='/'>Return to setup</a></body></html>"
    );
}
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    switch (event_id) {
    case WIFI_EVENT_STA_START:
        if ((xEventGroupGetBits(wifi_event_group) &
             EVENT_STA_CONFIGURED) != 0) {
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Initial connect request failed: %s",
                         esp_err_to_name(err));
            }
        }
        break;

    case WIFI_EVENT_STA_DISCONNECTED:
        xEventGroupClearBits(wifi_event_group, EVENT_WIFI_CONNECTED);
        xEventGroupSetBits(wifi_event_group, EVENT_WIFI_DISCONNECTED);
        ESP_LOGW(TAG, "Router Wi-Fi disconnected; setup portal remains available");
        /* No automatic retry loop: reconfigure from the setup page. */
        break;

    case WIFI_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "A client connected to %s", AP_NAME);
        break;

    case WIFI_EVENT_AP_STADISCONNECTED:
        ESP_LOGI(TAG, "A client disconnected from %s", AP_NAME);
        break;

    default:
        break;
    }
}

static void ip_event_handler(void *arg,
                             esp_event_base_t event_base,
                             int32_t event_id,
                             void *event_data)
{
    if (event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Router connection obtained IP: " IPSTR,
             IP2STR(&event->ip_info.ip));

    xEventGroupClearBits(wifi_event_group, EVENT_WIFI_DISCONNECTED);
    xEventGroupSetBits(wifi_event_group, EVENT_WIFI_CONNECTED);
}

static void apply_station_config(wifi_config_t *config)
{
    /* Disconnect the old station connection/attempt before changing config. */
    xEventGroupClearBits(wifi_event_group,
                         EVENT_WIFI_CONNECTED | EVENT_WIFI_DISCONNECTED);

    esp_err_t err = esp_wifi_disconnect();
    if (err == ESP_OK) {
        /* Only this network task waits; local control tasks are unaffected. */
        (void)xEventGroupWaitBits(
            wifi_event_group,
            EVENT_WIFI_DISCONNECTED,
            pdTRUE,
            pdTRUE,
            pdMS_TO_TICKS(2000)
        );
    }

    err = esp_wifi_set_config(WIFI_IF_STA, config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not save Wi-Fi configuration: %s",
                 esp_err_to_name(err));
        return;
    }

    xEventGroupSetBits(wifi_event_group, EVENT_STA_CONFIGURED);
    ESP_LOGI(TAG, "Applying new Wi-Fi configuration for SSID: %.32s",
             (const char *)config->sta.ssid);

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Connection request was not accepted: %s",
                 esp_err_to_name(err));
    }
}

void network_init(void)
{
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi event group");
        abort();
    }

    wifi_config_queue = xQueueCreate(WIFI_CONFIG_QUEUE_LENGTH,
                                     sizeof(wifi_config_t));
    if (wifi_config_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi configuration queue");
        abort();
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs recovery; erasing NVS partition");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (esp_netif_create_default_wifi_sta() == NULL ||
        esp_netif_create_default_wifi_ap() == NULL) {
        ESP_LOGE(TAG, "Failed to create default Wi-Fi network interfaces");
        abort();
    }

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_NAME,
            .ssid_len = sizeof(AP_NAME) - 1,
            .channel = 1,
            .password = AP_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    
    wifi_config_t saved_station_config = {0};
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &saved_station_config));
    if (saved_station_config.sta.ssid[0] != '\0') {
        ESP_LOGI(TAG, "Saved Wi-Fi credentials found");
        xEventGroupSetBits(wifi_event_group, EVENT_STA_CONFIGURED);
    } else {
        ESP_LOGI(TAG, "No saved Wi-Fi credentials; configure through the setup page");
    }

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL));

    
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(httpd_start(&http_server, &http_config));

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t wifi_uri = {
        .uri = "/wifi",
        .method = HTTP_POST,
        .handler = wifi_post_handler,
        .user_ctx = NULL,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(http_server, &root_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(http_server, &wifi_uri));

    ESP_LOGI(TAG, "Setup AP is available as '%s'", AP_NAME);
    ESP_LOGI(TAG, "Connect to %s and open http://192.168.4.1/", AP_NAME);
}

void network_task(void *pvParameters)
{
    (void)pvParameters;

    network_init();

    for (;;) {
        /*
         * Process configuration requests submitted by the browser.
         * The wait is confined to this task, not the fan/sensor tasks.
         */
        wifi_config_t requested_config;
        if (xQueueReceive(wifi_config_queue,
                          &requested_config,
                          pdMS_TO_TICKS(500)) == pdTRUE) {
            apply_station_config(&requested_config);
        }

        EventBits_t bits = xEventGroupGetBits(wifi_event_group);
        if ((bits & EVENT_WIFI_DISCONNECTED) != 0) {
            xEventGroupClearBits(wifi_event_group,
                                 EVENT_WIFI_DISCONNECTED);
            ESP_LOGW(TAG,
                     "Wi-Fi is offline. Reconfigure at http://192.168.4.1/");
        }
    }
}