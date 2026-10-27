#ifndef NETWORK_PROVISION_H
#define NETWORK_PROVISION_H

#include <stdbool.h>

/* Starts the Wi-Fi AP+STA interfaces and the local setup webpage. */
void network_init(void);

/* Run this in its own FreeRTOS task. */
void network_task(void *pvParameters);

/* Nonblocking status check for sensor/application tasks. */
bool network_is_connected(void);

#endif // NETWORK_PROVISION_H