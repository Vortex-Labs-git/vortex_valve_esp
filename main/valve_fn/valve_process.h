#ifndef VALVE_PROCESS_H
#define VALVE_PROCESS_H

#include "led_indicators.h"
#include "valve_motor.h"
#include "limit_switch.h"

extern Motor motor;
extern LimitSwitches closeLimit;
extern LimitSwitches openLimit;
extern LedIndicator redLED;
extern LedIndicator greenLED;

void init_valve_system(void);
int motor_open(void);
int motor_close(void);
int valve_set_position(int angle);


#endif // VALVE_PROCESS_H
