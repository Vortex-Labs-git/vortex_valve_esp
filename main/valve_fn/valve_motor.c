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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "valve_motor.h"

typedef struct {
    Motor *motor;
    int dutyCycle;
    int direction;  // 1=clockwise, -1=anticlockwise, 0=stop
} MotorTaskParam;

static MotorTaskParam motorParam;
static TaskHandle_t motorTaskHandle = NULL;
static portMUX_TYPE motorMux = portMUX_INITIALIZER_UNLOCKED;

static void motor_task(void *arg) {
    Motor *motor = motorParam.motor; 

    while (1) {
        int dir, duty;
        taskENTER_CRITICAL(&motorMux);
        dir  = motorParam.direction;
        duty = motorParam.dutyCycle;
        taskEXIT_CRITICAL(&motorMux);

        switch (dir) {
            case 1: // clockwise
                gpio_set_level(motor->motorIN1_PIN, 1);
                gpio_set_level(motor->motorIN2_PIN, 0);
                break;
            case -1: // anticlockwise
                gpio_set_level(motor->motorIN1_PIN, 0);
                gpio_set_level(motor->motorIN2_PIN, 1);
                break;
            case 0: // stop
            default:
                gpio_set_level(motor->motorIN1_PIN, 0);
                gpio_set_level(motor->motorIN2_PIN, 0);
                break;
        }

        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, motorParam.dutyCycle);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

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

    // Initialize motorParam and start motor task
    motorParam.motor = motor;
    motorParam.direction = 0;
    motorParam.dutyCycle = 0;

    if (!motorTaskHandle) {
        xTaskCreate(motor_task, "motor_task", 2048, NULL, 5, &motorTaskHandle);
    }
}

void motor_run_clk(Motor *motor, int dutyCycle) {
    taskENTER_CRITICAL(&motorMux);
    motorParam.direction = 1;
    motorParam.dutyCycle = dutyCycle;
    taskEXIT_CRITICAL(&motorMux);
}

void motor_run_aclck(Motor *motor, int dutyCycle) {
    taskENTER_CRITICAL(&motorMux);
    motorParam.direction = -1;
    motorParam.dutyCycle = dutyCycle;
    taskEXIT_CRITICAL(&motorMux);
}

void motor_stop(Motor *motor) {
    taskENTER_CRITICAL(&motorMux);
    motorParam.direction = 0;
    motorParam.dutyCycle = 0;
    taskEXIT_CRITICAL(&motorMux);
}