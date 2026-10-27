#include "network_provision.h"
#include "fan.h"

#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_main";

void tasks_init() {
    BaseType_t result = xTaskCreate(
        network_task,
        "network_task",
        6144,
        NULL,
        5,
        NULL
    );
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create network task");
    }
}

void app_main() {}