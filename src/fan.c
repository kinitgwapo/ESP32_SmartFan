#include "fan.h"
#include "network_provision.h"

#include <stdbool.h>

#include "driver/gpio.h"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>

#define INPUT_OFF GPIO_NUM_18
#define INPUT_LOW GPIO_NUM_19
#define INPUT_NORMAL GPIO_NUM_21
#define INPUT_HIGH GPIO_NUM_22

#define OUTPUT_OFF GPIO_NUM_23
#define OUTPUT_LOW GPIO_NUM_25
#define OUTPUT_NORMAL GPIO_NUM_26
#define OUTPUT_HIGH GPIO_NUM_27

#define DEBOUNCE_SAMPLE_MS    10   /* how often the task samples the buttons */
#define DEBOUNCE_STABLE_COUNT 3    /* samples in a row = 30 ms stable         */

typedef enum {
    FAN_OFF = 0,
    FAN_LOW,
    FAN_NORMAL,
    FAN_HIGH,
    FAN_SPEED_COUNT
} fan_speed_t;

typedef struct {
    gpio_num_t pin;
    bool stable_state;    /* last confirmed level: true = released */
    uint8_t counter;      /* how many samples in a row differ from it */
} debounced_input_t;

static debounced_input_t inputs[FAN_SPEED_COUNT] = {
    [FAN_OFF]    = { INPUT_OFF,    true, 0 },
    [FAN_LOW]    = { INPUT_LOW,    true, 0 },
    [FAN_NORMAL] = { INPUT_NORMAL, true, 0 },
    [FAN_HIGH]   = { INPUT_HIGH,   true, 0 },
};

static const gpio_num_t output_pins[FAN_SPEED_COUNT] = {
    [FAN_OFF]    = OUTPUT_OFF,
    [FAN_LOW]    = OUTPUT_LOW,
    [FAN_NORMAL] = OUTPUT_NORMAL,
    [FAN_HIGH]   = OUTPUT_HIGH,
};

void fan_init(void)
{
    gpio_config_t input_cfg = {
    .pin_bit_mask = BIT64(INPUT_OFF) |          /*!< GPIO pin: set with bit mask, each bit maps to a GPIO */
                    BIT64(INPUT_LOW) |
                    BIT64(INPUT_NORMAL) |
                    BIT64(INPUT_HIGH),
    .mode = GPIO_MODE_INPUT,                    /*!< GPIO mode: set input/output mode                     */
    .pull_up_en = GPIO_PULLUP_ENABLE,          /*!< GPIO pull-up                                         */
    .pull_down_en = GPIO_PULLDOWN_DISABLE,      /*!< GPIO pull-down                                       */
    .intr_type = GPIO_INTR_DISABLE              /*!< GPIO interrupt type                                  */
    };

    gpio_config_t output_cfg = {
    .pin_bit_mask = BIT64(OUTPUT_OFF) |         /*!< GPIO pin: set with bit mask, each bit maps to a GPIO */
                    BIT64(OUTPUT_LOW) |
                    BIT64(OUTPUT_NORMAL) |
                    BIT64(OUTPUT_HIGH),
    .mode = GPIO_MODE_OUTPUT,                   /*!< GPIO mode: set input/output mode                     */
    .pull_up_en = GPIO_PULLUP_DISABLE,          /*!< GPIO pull-up                                         */
    .pull_down_en = GPIO_PULLDOWN_DISABLE,      /*!< GPIO pull-down                                       */
    .intr_type = GPIO_INTR_DISABLE              /*!< GPIO interrupt type                                  */
    };

    ESP_ERROR_CHECK(gpio_config(&input_cfg));
    ESP_ERROR_CHECK(gpio_config(&output_cfg));
}

void fan_task(void *pvParameters)
{
    while(true) {
        if(network_is_connected()) {

        } 
    }
}