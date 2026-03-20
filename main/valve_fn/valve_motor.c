#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/ledc.h"

#include "valve_motor.h"





void motor_init(Motor *motor) {
    gpio_set_direction(motor->motorIN1_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(motor->motorIN2_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(motor->motorEN1_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(motor->motorIN1_PIN, 0);
    gpio_set_level(motor->motorIN2_PIN, 0);
    gpio_set_level(motor->motorEN1_PIN, 0);

    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 30000,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .gpio_num = motor->motorEN1_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel);
}

void motor_run_clk(Motor *motor, int dutyCycle) {
    gpio_set_level(motor->motorIN1_PIN, 1);
    gpio_set_level(motor->motorIN2_PIN, 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, dutyCycle);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void motor_run_aclck(Motor *motor, int dutyCycle) {
    gpio_set_level(motor->motorIN1_PIN, 0);
    gpio_set_level(motor->motorIN2_PIN, 1);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, dutyCycle);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void motor_stop(Motor *motor) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    gpio_set_level(motor->motorIN1_PIN, 0);
    gpio_set_level(motor->motorIN2_PIN, 0);
}
