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
    
    /*
     * Initialize/start your independent fan and sensor tasks here.
     * They should use network_is_connected() before sending data,
     * rather than waiting for Wi-Fi.
     */

    result = xTaskCreate(
        fan_task,
        "fan_task",
        2048,
        NULL,
        3,
        NULL
    );
    if(result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create fan task");
    }
}

void app_main(void) {
    fan_init();
    tasks_init();

    while (true) {
        /* Replace this delay with your app-level work if needed. */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}