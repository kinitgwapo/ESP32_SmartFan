#ifndef FAN_H
#define FAN_H

/* Configure & Initialize fan control pins */
void fan_init(void);

/* FreeRTOS Task */
void fan_task(void *pvParameters);

#endif /* FAN_H */