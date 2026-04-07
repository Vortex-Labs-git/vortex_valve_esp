
#include "sdkconfig.h" 
#include "esp_log.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "global_var.h"
#include "led_indicators.h"
#include "valve_motor.h"
#include "limit_switch.h"
#include "pot_read.h"
#include "valve_process.h"


/* =================== PIN CONFIGURATION =================== */
#define MOTOR_EN_PIN        CONFIG_MOTOR_EN_PIN
#define MOTOR_IN1_PIN       CONFIG_MOTOR_IN1_PIN
#define MOTOR_IN2_PIN       CONFIG_MOTOR_IN2_PIN

#define POTENTIOMETER_PIN   CONFIG_POTENTIOMETER_PIN

#define RED_LED_PIN         CONFIG_RED_LED_PIN
#define GREEN_LED_PIN       CONFIG_GREEN_LED_PIN

/* =================== CALIBRATION =================== */
#define ADC_MIN_VALID   1400
#define ADC_MAX_VALID   2600
#define ADC_CLOSE   1500   // 0°
#define ADC_OPEN    2500    // 90°

/* =================== HARDWARE OBJECTS =================== */
Motor motor = { MOTOR_IN1_PIN, MOTOR_IN2_PIN, MOTOR_EN_PIN, 0 };
PotSensor potentiometer = {POTENTIOMETER_PIN, 0, ADC_CLOSE, ADC_OPEN};
LedIndicator redLED = { RED_LED_PIN };
LedIndicator greenLED = { GREEN_LED_PIN };


static const char *TAG = "VALVE_PROCESS";
typedef struct {
    float kp;
    float ki;
    float kd;

    float prev_error;
    float integral;
} PIDController;

PIDController pidValue = {
        .kp = 2.0,
        .ki = 0.01,
        .kd = 0.4,
        .prev_error = 0,
        .integral = 0
    };



float pid_compute(PIDController *pid, float setpoint, float measured)
{
    float error = setpoint - measured;

    pid->integral += error;

    // Anti-windup
    if (pid->integral > 1000) pid->integral = 1000;
    if (pid->integral < -1000) pid->integral = -1000;

    float derivative = error - pid->prev_error;

    float output = (pid->kp * error) + (pid->ki * pid->integral) + (pid->kd * derivative);

    pid->prev_error = error;

    return output;
}




void init_valve_system(void) {
    motor_init(&motor);
    pot_sensor_init(&potentiometer);

    led_init(&redLED);
    led_init(&greenLED);

    led_on(&redLED);
    led_on(&greenLED);

    vTaskDelay(pdMS_TO_TICKS(1000));

    led_off(&redLED);
    led_off(&greenLED);

    ESP_LOGI(TAG, "Valve system initialized");
}


int motor_set_angle(int target_angle)
{   
    pidValue.integral = 0;
    pidValue.prev_error = 0;

    const float tolerance = 2.0;     // degrees
    const int timeout_ms = 10000;
    unsigned long start = xTaskGetTickCount() * portTICK_PERIOD_MS;

    ESP_LOGI(TAG, "Moving to angle: %d", target_angle);

    while (1)
    {
        int adc = pot_read_filtered(&potentiometer);
        float current_angle = pot_to_angle(&potentiometer, adc);

        float control = pid_compute(&pidValue, target_angle, current_angle);

        int duty = (int)fabs(control);

        // Limit PWM
        if (duty > 200) duty = 200;

        // Minimum power to overcome friction
        if (duty < 80) duty = 80;

        ESP_LOGI(TAG,
            "[PID] Target:%d | Current:%.2f | OUT:%.2f | PWM:%d | ADC:%d",
            target_angle,
            current_angle,
            control,
            duty,
            adc
        );

        // Deadband (prevent jitter)
        if (fabs(control) < 5) {
            ESP_LOGI(TAG, "Deadband reached → motor stop");
            motor_stop(&motor);
        }
        else if (control > 0) {
            ESP_LOGI(TAG, "Direction: OPEN (ACLK)");
            motor_run_aclck(&motor, duty);  // OPEN
        }
        else {
            ESP_LOGI(TAG, "Direction: CLOSE (CLK)");
            motor_run_clk(&motor, duty);    // CLOSE
        }


        // Stop condition
        if (fabs(target_angle - current_angle) < tolerance) {
            motor_stop(&motor);
            vTaskDelay(pdMS_TO_TICKS(100));
            ESP_LOGI(TAG, "Target reached");
            break;
        }

        // Timeout safety
        if ((xTaskGetTickCount() * portTICK_PERIOD_MS) - start > timeout_ms) {
            motor_stop(&motor);
            led_on(&redLED);

            ESP_LOGE(TAG, "Timeout error");

            xSemaphoreTake(valveMutex, portMAX_DELAY);
            sprintf(valveData.error_msg, "PID timeout");
            xSemaphoreGive(valveMutex);

            return -1;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // Update shared state
    xSemaphoreTake(valveMutex, portMAX_DELAY);
    valveData.angle = target_angle;
    xSemaphoreGive(valveMutex);

    led_off(&redLED);

    return 0;
}


int valve_test(void)
{
    int adc = pot_read_filtered(&potentiometer);

    xSemaphoreTake(valveMutex, portMAX_DELAY);

    if (adc == 0) {
        sprintf(valveData.error_msg, "Pot not detected ! ....");
        xSemaphoreGive(valveMutex);
        ESP_LOGE(TAG, "Pot not detected ! ....");
        return 101;
    }

    if (adc < ADC_MIN_VALID || adc > ADC_MAX_VALID) {
        sprintf(valveData.error_msg, "Pot out of range: %d", adc);
        xSemaphoreGive(valveMutex);
        ESP_LOGE(TAG, "Pot out of range: %d", adc);
        return 102;
    }

    float angle = pot_to_angle(&potentiometer, adc);

    // Additional sanity check (optional but good)
    if (angle < -5 || angle > 95) {
        sprintf(valveData.error_msg, "Angle invalid: %.2f", angle);
        xSemaphoreGive(valveMutex);
        ESP_LOGE(TAG, "Angle invalid: %.2f", angle);
        return 103;
    }

    valveData.angle = angle;

    xSemaphoreGive(valveMutex);
    ESP_LOGI(TAG, "Pot detect: %.2f", angle);
    return 0;
}





