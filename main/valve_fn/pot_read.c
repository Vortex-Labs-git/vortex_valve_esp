
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "pot_read.h"




// GPIO → ADC1 channel mapping
static adc1_channel_t gpio_to_adc(uint8_t gpio)
{
    switch (gpio) {
        case 32: return ADC1_CHANNEL_4;
        case 33: return ADC1_CHANNEL_5;
        case 34: return ADC1_CHANNEL_6;
        case 35: return ADC1_CHANNEL_7;
        case 36: return ADC1_CHANNEL_0;
        case 39: return ADC1_CHANNEL_3;
        default: return ADC1_CHANNEL_MAX;
    }
}


void pot_sensor_init(PotSensor *sensor) {
    sensor->channel = gpio_to_adc(sensor->pin);

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(sensor->channel, ADC_ATTEN_DB_11);

}

int pot_sensor_read(PotSensor *sensor) {
    return adc1_get_raw(sensor->channel);
}


int pot_read_filtered(PotSensor *sensor)
{
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += pot_sensor_read(sensor);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return sum / 10;
}


float pot_to_angle(PotSensor *sensor, int adc)
{
    float angle = (float)(adc - sensor->adc_close) * 90.0f / (sensor->adc_open - sensor->adc_close);

    return angle;
}