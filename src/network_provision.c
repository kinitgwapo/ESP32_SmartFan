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