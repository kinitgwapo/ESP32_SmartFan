#include "fan.h"

#include <stdbool.h>

#include "driver/gpio.h"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define INPUT_OFF GPIO_NUM_18
#define INPUT_LOW GPIO_NUM_19
#define INPUT_NORMAL GPIO_NUM_21
#define INPUT_HIGH GPIO_NUM_22

#define OUTPUT_OFF GPIO_NUM_23
#define OUTPUT_LOW GPIO_NUM_25
#define OUTPUT_NORMAL GPIO_NUM_26
#define OUTPUT_HIGH GPIO_NUM_27

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

    ESP_ERROR_CHECK(gpio_config(&input_cfg));
}