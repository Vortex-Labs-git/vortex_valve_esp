
#include "sdkconfig.h" 
#include "esp_log.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "global_fn/global_var.h"
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


/* =================== MOTION TUNING (bench-tune these) =================== */
/*
 * This actuator cannot do fine proportional taper: below ~200 duty it will
 * not break static friction, and the usable band is narrow (200-220). So this
 * is intentionally bang-bang control with a brief friction-breaking "kick",
 * NOT a PID.
 */
#define DUTY_RUN            210     /* steady drive, mid of the 200-220 band   */
#define DUTY_KICK           220     /* higher kick to break static friction    */
#define KICK_MS             120     /* TUNE: how long the valve needs to start
                                       moving from cold. Too short re-introduces
                                       false stall trips during friction-break. */

/* Pulsed approach: within APPROACH_DEG of target, stop driving continuously
and instead nudge-then-coast so momentum doesn't overshoot the band. */
#define APPROACH_DEG        8.0f    /* within this, switch to pulsed creep     */
#define PULSE_ON_MS         30      /* TUNE: shorter = less overshoot, slower  */
#define PULSE_OFF_MS        40      /* coast + settle before re-measuring      */

 
#define ANGLE_TOLERANCE     2.0f    /* degrees: close enough to target         */
#define MOVE_TIMEOUT_MS     10000   /* hard safety net                         */
 
/* Stall detection (operates in RAW ADC counts to sidestep the
 * non-linear ADC->angle mapping). */
#define ADC_PROGRESS_MIN    8       /* TUNE: min ADC change that counts as
                                       "moving". Set just above pot noise floor */
#define STALL_TIMEOUT_MS    1500    /* driven this long with no progress = stuck */
#define END_BAND            60      /* TUNE: ADC window around a calibrated
                                       end-stop that is treated as "at the end" */
 



/* =================== HARDWARE OBJECTS =================== */
Motor motor = { MOTOR_IN1_PIN, MOTOR_IN2_PIN, MOTOR_EN_PIN, 0 };
PotSensor potentiometer = {POTENTIOMETER_PIN, 0, 1500, 2500};
LedIndicator redLED = { RED_LED_PIN };
LedIndicator greenLED = { GREEN_LED_PIN };


static const char *TAG = "VALVE_PROCESS";
 
 
/* small helper for elapsed milliseconds */
static inline unsigned long millis(void) {
    return (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}



void init_valve_system(void) {
    motor_init(&motor);
    pot_sensor_init(&potentiometer);

    led_init(&redLED);
    led_init(&greenLED);

    led_on(&redLED);
    led_on(&greenLED);

    vTaskDelay(pdMS_TO_TICKS(2000));

    led_off(&redLED);
    led_off(&greenLED);

    int adc = pot_read_filtered(&potentiometer);
    float current_angle = pot_to_angle(&potentiometer, adc);
    bool open_now  = (current_angle >= 88);
    bool close_now = (current_angle <= 4);

    xSemaphoreTake(valveMutex, portMAX_DELAY);
    valveData.encoder_value = adc;
    valveData.angle = current_angle;
    valveData.is_open = open_now;
    valveData.is_close = close_now;
    xSemaphoreGive(valveMutex);

    ESP_LOGI(TAG, "Valve system initialized");
}

void pot_read_update(void)
{
    int adc = pot_read_filtered(&potentiometer);
    float current_angle = pot_to_angle(&potentiometer, adc);
    
    bool open_now  = (current_angle >= 88);
    bool close_now = (current_angle <= 2);

    xSemaphoreTake(valveMutex, portMAX_DELAY);
    valveData.encoder_value = adc;
    valveData.angle = current_angle;
    valveData.is_open = open_now;
    valveData.is_close = close_now;
    xSemaphoreGive(valveMutex);
}

void motor_rotate_clk(void)
{
    motor_run_clk(&motor, 200);
    vTaskDelay(pdMS_TO_TICKS(200));
    motor_stop(&motor);
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "Motor rotate bit clockwise ");
    pot_read_update();
}

void motor_rotate_aclk(void)
{
    motor_run_aclck(&motor, 200);
    vTaskDelay(pdMS_TO_TICKS(100));
    motor_stop(&motor);
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "Motor rotate bit anticlockwise ");
    pot_read_update();
}





/*
 * Move the valve to target_angle.
 *
 * Control model: bang-bang with a friction-breaking kick.
 *   Phase 1 (0 .. KICK_MS): drive at DUTY_KICK. Movement is NOT expected yet,
 *                           so stall detection is suspended here.
 *   Phase 2 (after KICK_MS): drive at DUTY_RUN. A healthy valve is moving now,
 *                            so "max effort + no ADC change" => genuine stall.
 *
 * Return codes:
 *    0  success (reached tolerance band)
 *   -1  hard timeout
 *   -2  invalid feedback (bad calibration / dead pot)
 *   -3  stall mid-travel (jam or sensor fault)
 *   -4  reached a mechanical end-stop before the target
 *       (normally means calibration drifted so the target is unreachable;
 *        a correctly calibrated valve exits via the tolerance band first)
 */
int motor_set_angle( int target_angle) {
    
    const unsigned long start = millis();
 
    ESP_LOGI(TAG, "Moving to angle: %d", target_angle);
 
    int last_adc = pot_read_filtered(&potentiometer);
    unsigned long last_progress_ms = start;


    while (1) {
        int adc = pot_read_filtered(&potentiometer);
        float current_angle = pot_to_angle(&potentiometer, adc);

        bool open_now  = (current_angle >= 88);
        bool close_now = (current_angle <= 2);

        xSemaphoreTake(valveMutex, portMAX_DELAY);
        valveData.encoder_value = adc;
        valveData.angle = current_angle;
        valveData.is_open = open_now;
        valveData.is_close = close_now;
        xSemaphoreGive(valveMutex);

        unsigned long now_ms  = millis();
        unsigned long elapsed = now_ms - start;

        if (current_angle < 0.0f) {
            motor_stop(&motor);
            led_on(&redLED);
            ESP_LOGE(TAG, "Invalid angle reading (calibration?). Aborting move.");

            xSemaphoreTake(valveMutex, portMAX_DELAY);
            snprintf(valveData.error_msg, sizeof(valveData.error_msg),"Invalid feedback (calib?)");
            xSemaphoreGive(valveMutex);

            return -2;
        }

        float error = target_angle - current_angle;

        /* ---- Reached target ---- */
        if (fabs(error) <= ANGLE_TOLERANCE) {
            motor_stop(&motor);
            vTaskDelay(pdMS_TO_TICKS(100));
            ESP_LOGI(TAG, "Target reached (angle=%.2f)", current_angle);
            break;
        }

        bool in_kick_phase = (elapsed < KICK_MS);

        /* ---- Drive ---- */
        if (!in_kick_phase && fabs(error) < APPROACH_DEG) {
            /* PHASE 3: pulsed creep. Nudge, then coast+settle, so we don't carry momentum past the tolerance band. */
            if (error > 0) motor_run_aclck(&motor, DUTY_RUN);
            else           motor_run_clk(&motor, DUTY_RUN);
            vTaskDelay(pdMS_TO_TICKS(PULSE_ON_MS));
            motor_stop(&motor);
 
            ESP_LOGI(TAG, "[MOVE] tgt:%d cur:%.2f err:%.2f PULSE adc:%d", target_angle, current_angle, error, adc);
 
            vTaskDelay(pdMS_TO_TICKS(PULSE_OFF_MS));
        } else {
            /* PHASE 1 (kick) or PHASE 2 (continuous approach). */
            int duty = in_kick_phase ? DUTY_KICK : DUTY_RUN;
            if (error > 0) motor_run_aclck(&motor, duty);
            else           motor_run_clk(&motor, duty);
 
            ESP_LOGI(TAG, "[MOVE] tgt:%d cur:%.2f err:%.2f duty:%d adc:%d %s", target_angle, current_angle, error, duty, adc, in_kick_phase ? "(kick)" : "");
 
            vTaskDelay(pdMS_TO_TICKS(20));
        }




        /* ---- Stall detection: only AFTER the friction-break window ---- */
        if (in_kick_phase) {
            /* Movement not expected yet; keep the progress clock fresh
               so the friction-break window never counts as a stall. */
            last_adc = adc;
            last_progress_ms = now_ms;
        } else {
            if (abs(adc - last_adc) >= ADC_PROGRESS_MIN) {
                /* We moved — reset the stall clock. */
                last_adc = adc;
                last_progress_ms = now_ms;
            } else if (now_ms - last_progress_ms > STALL_TIMEOUT_MS) {
                /* Max effort applied past the friction window, still no ADC
                   change. Distinguish "hit a mechanical end" from "jammed
                   mid-travel" by WHERE it stopped, not THAT it stopped. */
                motor_stop(&motor);
 
                bool near_end =
                    (abs(adc - potentiometer.adc_open)  < END_BAND) ||
                    (abs(adc - potentiometer.adc_close) < END_BAND);
 
                if (near_end) {
                    led_off(&redLED);
                    ESP_LOGW(TAG, "End-stop reached (adc=%d) before target %d", adc, target_angle);
                    xSemaphoreTake(valveMutex, portMAX_DELAY);
                    snprintf(valveData.error_msg, sizeof(valveData.error_msg), "End-stop before target %d", target_angle);
                    xSemaphoreGive(valveMutex);
                    return -4;
                } else {
                    led_on(&redLED);
                    ESP_LOGE(TAG, "Stall mid-travel (adc=%d) - jam or sensor fault", adc);
                    xSemaphoreTake(valveMutex, portMAX_DELAY);
                    snprintf(valveData.error_msg, sizeof(valveData.error_msg), "Valve stalled mid-travel");
                    xSemaphoreGive(valveMutex);
                    return -3;
                }
            }
        }


        /* ---- Hard timeout safety net ---- */
        if (elapsed > MOVE_TIMEOUT_MS) {
            motor_stop(&motor);
            led_on(&redLED);
            ESP_LOGE(TAG, "Move timeout");
 
            xSemaphoreTake(valveMutex, portMAX_DELAY);
            snprintf(valveData.error_msg, sizeof(valveData.error_msg), "Move timeout");
            xSemaphoreGive(valveMutex);
            return -1;
        }


        vTaskDelay(pdMS_TO_TICKS(20));
    }

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

    int min_valid = potentiometer.adc_close - 100;
    int max_valid = potentiometer.adc_open + 100;

    if (adc < min_valid || adc > max_valid) {
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





