#ifndef POT_READ_H
#define POT_READ_H

#include <stdint.h>
#include "driver/adc.h"

typedef struct {
    uint8_t pin;              // GPIO number (e.g., 34)
    adc1_channel_t channel;  // ADC channel (auto-mapped)

    int adc_close;   // ADC value at 0°
    int adc_open;    // ADC value at 90°
} PotSensor;


void pot_sensor_init(PotSensor *sensor);
int pot_sensor_read(PotSensor *sensor);
int pot_read_filtered(PotSensor *sensor);
float pot_to_angle(PotSensor *sensor, int adc);


#endif // POT_READ_H
