#ifndef VALVE_PROCESS_H
#define VALVE_PROCESS_H

#include <stdint.h>

#include "led_indicators.h"
#include "valve_motor.h"
#include "limit_switch.h"
#include "pot_read.h"

extern Motor motor;
extern PotSensor potentiometer;
extern LedIndicator redLED;
extern LedIndicator greenLED;

void init_valve_system(void);
int valve_test(void);
int motor_set_angle(int target_angle);


#endif // VALVE_PROCESS_H
