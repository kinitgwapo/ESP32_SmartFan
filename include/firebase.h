#ifndef FIREBASE_H
#define FIREBASE_H

#include <esp_err.h>
#include "fan.h"

esp_err_t firebase_start_auth_listener(void);

/* Notify Firebase reporting logic that actual fan state changed */
void firebase_state_changed(fan_speed_t speed);

#endif /* FIREBASE_H */