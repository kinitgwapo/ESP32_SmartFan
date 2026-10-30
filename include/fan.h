#ifndef FAN_H
#define FAN_H

#include "stdint.h"
#include "esp_err.h"

typedef enum {
    FAN_OFF = 0,
    FAN_LOW,
    FAN_NORMAL,
    FAN_HIGH,
    FAN_SPEED_COUNT
} fan_speed_t;

typedef struct {
    fan_speed_t speed;
    int64_t command_id;
} fan_command_t;

/* Configure & Initialize fan control pins */
void fan_init(void);

/* FreeRTOS Task */
void fan_task(void *pvParameters);

/* Submit a validated remote command to fan_task */
esp_err_t fan_command_submit(const fan_command_t *command);

/* Return the currently applied fan speed */
fan_speed_t fan_get_speed(void);

#endif /* FAN_H */