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