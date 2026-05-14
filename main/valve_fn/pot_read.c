
#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "pot_read.h"
#include "esp_log.h"

#include "global_fn/global_var.h" 





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

    if (sensor->channel == ADC1_CHANNEL_MAX) {
        ESP_LOGE("POT", "Invalid GPIO for ADC: %d", sensor->pin);
        return;
    }

    xSemaphoreTake(valveMutex, portMAX_DELAY);
    sensor->adc_close = (int)valveData.close_limit_encode;
    sensor->adc_open  = (int)valveData.open_limit_encode;
    xSemaphoreGive(valveMutex);

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(sensor->channel, ADC_ATTEN_DB_11);

}

void pot_update_calibration(PotSensor *sensor)
{
    xSemaphoreTake(valveMutex, portMAX_DELAY);

    sensor->adc_close = (int)valveData.close_limit_encode;
    sensor->adc_open  = (int)valveData.open_limit_encode;

    xSemaphoreGive(valveMutex);
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
    int close = sensor->adc_close;
    int open  = sensor->adc_open;

    // Validate calibration
    if (open <= close) {
        return -1.0f; // invalid
    }

    // Clamp ADC into calibrated range
    if (adc < close) adc = close;
    if (adc > open)  adc = open;

    float angle = (float)(adc - close) * 90.0f / (open - close);

    // Snap endpoints to avoid jitter
    if (fabsf(angle) < 1.0f) angle = 0.0f;
    if (fabsf(angle - 90.0f) < 1.0f) angle = 90.0f;

    return angle;
}