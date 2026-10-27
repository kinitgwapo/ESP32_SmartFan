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