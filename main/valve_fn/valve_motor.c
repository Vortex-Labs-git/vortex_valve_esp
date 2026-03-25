/**
 * @file valve_motor.c
 * @brief Motor driver for ESP32-controlled valve
 *
 * This module provides functions to initialize and control a DC motor
 * using GPIO pins and ESP32's LEDC PWM driver. Supports:
 *  - Clockwise rotation
 *  - Anti-clockwise rotation
 *  - Motor stop
 */

#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/ledc.h"

#include "valve_motor.h"

/**
 * @brief Initialize motor GPIOs and PWM driver
 *
 * Configures:
 *  - Motor direction pins (IN1, IN2) as outputs
 *  - Motor enable pin (EN1) as PWM output
 *  - LEDC timer and channel for PWM control
 *
 * @param motor Pointer to Motor struct containing pin numbers
 */
void motor_init(Motor *motor) {
    /* Set GPIO directions */
    gpio_set_direction(motor->motorIN1_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(motor->motorIN2_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(motor->motorEN1_PIN, GPIO_MODE_OUTPUT);
    /* Set initial output levels to 0 (motor stopped) */
    gpio_set_level(motor->motorIN1_PIN, 0);
    gpio_set_level(motor->motorIN2_PIN, 0);
    gpio_set_level(motor->motorEN1_PIN, 0);

    /* Configure LEDC PWM timer */
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 30000,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0
    };
    ledc_timer_config(&ledc_timer);

    /* Configure LEDC channel for motor enable pin */
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

/**
 * @brief Run motor clockwise
 *
 * Sets motor direction pins for clockwise rotation and updates PWM duty.
 *
 * @param motor Pointer to Motor struct
 * @param dutyCycle PWM duty cycle (0-255 for 8-bit resolution)
 */
void motor_run_clk(Motor *motor, int dutyCycle) {
    gpio_set_level(motor->motorIN1_PIN, 1);
    gpio_set_level(motor->motorIN2_PIN, 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, dutyCycle);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/**
 * @brief Run motor anti-clockwise
 *
 * Sets motor direction pins for anti-clockwise rotation and updates PWM duty.
 *
 * @param motor Pointer to Motor struct
 * @param dutyCycle PWM duty cycle (0-255 for 8-bit resolution)
 */
void motor_run_aclck(Motor *motor, int dutyCycle) {
    gpio_set_level(motor->motorIN1_PIN, 0);
    gpio_set_level(motor->motorIN2_PIN, 1);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, dutyCycle);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/**
 * @brief Stop motor
 *
 * Stops motor rotation by setting PWM duty to 0 and clearing direction pins.
 *
 * @param motor Pointer to Motor struct
 */
void motor_stop(Motor *motor) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    gpio_set_level(motor->motorIN1_PIN, 0);
    gpio_set_level(motor->motorIN2_PIN, 0);
}
