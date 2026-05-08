/**
 * @file led_indicators.c
 * @brief LED management task for ESP32 Smart Valve Controller
 *
 * This module handles multiple LEDs with different operating modes:
 *  - ON
 *  - OFF
 *  - BLINK (fixed period)
 *  - BLINK2 (asymmetric ON/OFF periods)
 *
 * Features:
 *  - Central FreeRTOS task updates all LEDs in parallel
 *  - Timing handled using FreeRTOS ticks (non-blocking)
 *  - Dynamic mode switching at runtime
 *  - Logging for debug and monitoring
 */

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"

#include "led_indicators.h"


/**
 * @brief Maximum number of LEDs supported
 */
#define MAX_LED_COUNT        3

/**
 * @brief Task execution period for LED task in milliseconds
 *
 * Determines how often the LED states are updated.
 */
#define LED_TASK_PERIOD_MS  20

static const char *TAG_INDICATOR = "LED_INDICATOR";

/**
 * @brief Internal LED structure
 *
 * Holds the state and timing information for each LED.
 */
typedef struct {
    LedIndicator *pub;
    led_mode_t mode;
    bool state;
    uint32_t on_ms;
    uint32_t off_ms;
    TickType_t last_tick;
} LedInternal;


/**
 * @brief Array storing internal LED objects
 */
static LedInternal leds[MAX_LED_COUNT];
static uint8_t led_count = 0;


/**
 * @brief Find internal LED structure from public LED object
 *
 * @param led Pointer to public LED object
 * @return Pointer to internal LED structure, or NULL if not found
 */
static LedInternal *find_internal_led(LedIndicator *led)
{
    for (int i = 0; i < led_count; i++) {
        if (leds[i].pub == led)
            return &leds[i];
    }
    return NULL;
}



/**
 * @brief FreeRTOS task to update all LED states
 *
 * Responsibilities:
 *  - Reads current mode and timing from internal LED structures
 *  - Toggles pins according to mode and elapsed time
 *  - Runs periodically every LED_TASK_PERIOD_MS
 *
 * @param arg Not used
 */
static void led_task(void *arg)
{
    while (1) {
        TickType_t now = xTaskGetTickCount();

        for (int i = 0; i < led_count; i++) {
            LedInternal *led_internal = &leds[i];

            switch (led_internal->mode) {

                case LED_MODE_ON:
                    gpio_set_level(led_internal->pub->pin, 1);
                    led_internal->state = true;
                    break;

                case LED_MODE_OFF:
                    gpio_set_level(led_internal->pub->pin, 0);
                    led_internal->state = false;
                    break;

                case LED_MODE_BLINK:
                    if (now - led_internal->last_tick >= pdMS_TO_TICKS(led_internal->on_ms)) {
                        led_internal->last_tick = now;
                        led_internal->state ^= 1;
                        gpio_set_level(led_internal->pub->pin, led_internal->state);
                    }
                    break;

                case LED_MODE_BLINK2: {
                    uint32_t interval =
                        led_internal->state ? led_internal->on_ms : led_internal->off_ms;
                    if (now - led_internal->last_tick >= pdMS_TO_TICKS(interval)) {
                        led_internal->last_tick = now;
                        led_internal->state ^= 1;
                        gpio_set_level(led_internal->pub->pin, led_internal->state);
                    }
                    break;
                }

                default:
                    break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(LED_TASK_PERIOD_MS));
    }
}


/**
 * @brief Initialize an LED object
 *
 * Configures GPIO, initializes internal state, and ensures the LED task is running.
 *
 * @param led Pointer to public LED object
 */
void led_init(LedIndicator *led)
{
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << led->pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&io_conf);
    
    gpio_set_level(led->pin, 0);

    if (led_count < MAX_LED_COUNT) {
        leds[led_count++] = (LedInternal){
            .pub = led,
            .mode = LED_MODE_OFF,
            .state = false,
            .last_tick = xTaskGetTickCount()
        };
    }

    static bool task_started = false;
    if (!task_started) {
        task_started = true;
        xTaskCreate(led_task, "led_task", 2048, NULL, 5, NULL);
    }
}


/**
 * @brief Set LED to ON mode
 *
 * @param led Pointer to public LED object
 */
void led_on(LedIndicator *led)
{
    LedInternal *internal_led = find_internal_led(led);

    if (internal_led->mode == LED_MODE_ON)
        return;
    ESP_LOGI(TAG_INDICATOR, "LED ON called on pin %d\n", led->pin);

    if (internal_led) internal_led->mode = LED_MODE_ON;
}

/**
 * @brief Set LED to OFF mode
 *
 * @param led Pointer to public LED object
 */
void led_off(LedIndicator *led)
{
    LedInternal *internal_led = find_internal_led(led);

    if (internal_led->mode == LED_MODE_OFF)
        return;

    ESP_LOGI(TAG_INDICATOR, "LED OFF called on pin %d\n", led->pin);

    if (internal_led) internal_led->mode = LED_MODE_OFF;
}

/**
 * @brief Set LED to blink with fixed period
 *
 * @param led Pointer to public LED object
 * @param period_ms Blink period in milliseconds
 */
void led_blink(LedIndicator *led, uint32_t period_ms)
{
    LedInternal *internal_led = find_internal_led(led);

    if (internal_led->mode == LED_MODE_BLINK && internal_led->on_ms == period_ms)
        return;

    ESP_LOGI(TAG_INDICATOR, "LED BLINK called on pin %d with period %ld\n", led->pin, period_ms);

    if (internal_led) {
        internal_led->mode = LED_MODE_BLINK;
        internal_led->on_ms = period_ms;
        internal_led->last_tick = xTaskGetTickCount();
    }
}

/**
 * @brief Set LED to asymmetric blink (different ON and OFF durations)
 *
 * @param led Pointer to public LED object
 * @param on_ms ON duration in milliseconds
 * @param off_ms OFF duration in milliseconds
 */
void led_blink2(LedIndicator *led, uint32_t on_ms, uint32_t off_ms)
{
    LedInternal *internal_led = find_internal_led(led);

    if (internal_led->mode == LED_MODE_BLINK2 && internal_led->on_ms == on_ms && internal_led->off_ms == off_ms)
        return;

    ESP_LOGI(TAG_INDICATOR, "LED BLINK2 called on pin %d with on_ms %ld and off_ms %ld\n", led->pin, on_ms, off_ms);

    if (internal_led) {
        internal_led->mode = LED_MODE_BLINK2;
        internal_led->on_ms = on_ms;
        internal_led->off_ms = off_ms;
        internal_led->last_tick = xTaskGetTickCount();
    }
}