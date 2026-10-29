#include "fan.h"
#include "network_provision.h"

#include <stdbool.h>

#include "driver/gpio.h"

#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

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
#define DEBOUNCE_STABLE_COUNT 3    /* samples in a row = 30 ms stable        */
#define FAN_COMMAND_QUEUE_LENGTH 5

static const char *TAG = "Fan";

typedef struct {
    gpio_num_t pin;
    bool stable_state;    /* last confirmed level: true = released */
    uint8_t counter;      /* how many samples in a row differ from it */
} debounced_input_t;

QueueHandle_t fan_command_queue = NULL;

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

/* Returns true once per confirmed button press. */
static bool debounce_update(debounced_input_t *input)
{
    bool raw = gpio_get_level(input->pin);

    if (raw == input->stable_state) {
        input->counter = 0;          /* no change, reset */
        return false;
    }

    if (++input->counter >= DEBOUNCE_STABLE_COUNT) {
        input->stable_state = raw;   /* change confirmed */
        input->counter = 0;
        return raw == false;         /* pressed = pin went low */
    }

    return false;                    /* still waiting for it to settle */
}

/* Turn all outputs off first, then the selected one on (break before make). */
static void fan_set_speed(fan_speed_t speed)
{
    for (int i = 0; i < FAN_SPEED_COUNT; i++) {
        gpio_set_level(output_pins[i], 0);
    }
    gpio_set_level(output_pins[speed], 1);
}

void fan_init(void)
{
    fan_command_queue = xQueueCreate(FAN_COMMAND_QUEUE_LENGTH, sizeof(fan_command_t));
    if(fan_command_queue == NULL) ESP_ERROR_CHECK(ESP_ERR_NO_MEM);

    gpio_config_t input_cfg = {
    .pin_bit_mask = BIT64(INPUT_OFF) |          /*!< GPIO pin: set with bit mask, each bit maps to a GPIO */
                    BIT64(INPUT_LOW) |
                    BIT64(INPUT_NORMAL) |
                    BIT64(INPUT_HIGH),
    .mode = GPIO_MODE_INPUT,                    /*!< GPIO mode: set input/output mode                     */
    .pull_up_en = GPIO_PULLUP_ENABLE,           /*!< GPIO pull-up                                         */
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

    fan_set_speed(FAN_OFF);
}

esp_err_t fan_command_submit(const fan_command_t *command)
{
    if(command == NULL || command->speed < FAN_OFF || command->speed > FAN_HIGH) return ESP_ERR_INVALID_ARG;

    if(fan_command_queue == NULL) return ESP_ERR_INVALID_STATE;

    if(xQueueSend(fan_command_queue, command, 0) != pdPASS) {
        ESP_LOGW(TAG, "Command queue full; command %ld was not queued", (long)command->command_id);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

void fan_task(void *pvParameters)
{
    (void)pvParameters;

    while (true) {
        fan_command_t command;

        if(xQueueReceive(fan_command_queue, &command, 0) == pdPASS) {
            if(command.speed >= FAN_OFF && command.speed < FAN_SPEED_COUNT) {
                ESP_LOGI(TAG, "Applying remote command: id=%ld, speed=%d",
                              (long)command.command_id,
                              (int)command.speed
                );

                fan_set_speed(command.speed);
            }



            static const char *speed_names[] = { "off", "low", "normal", "high" };

            ESP_LOGI(TAG, "Received firebase command: id=%ld, speed=%s "
                          "(Logging only; GPIO unchanged)",
                          (long)command.command_id,
                          speed_names[command.speed]
            );
        }

        for (int i = 0; i < FAN_SPEED_COUNT; i++) {
            if (debounce_update(&inputs[i])) {
                fan_set_speed((fan_speed_t)i);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_SAMPLE_MS));
    }
}