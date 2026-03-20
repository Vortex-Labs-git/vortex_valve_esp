#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"

#include "led_indicators.h"


#define MAX_LED_COUNT        3
#define LED_TASK_PERIOD_MS  20

static const char *TAG_INDICATOR = "LED_INDICATOR";


// Internal structure to manage LED states
typedef struct {
    LedIndicator *pub;
    led_mode_t mode;
    bool state;
    uint32_t on_ms;
    uint32_t off_ms;
    TickType_t last_tick;
} LedInternal;


// Define LED object array
static LedInternal leds[MAX_LED_COUNT];
static uint8_t led_count = 0;


// LED object finder
static LedInternal *find_internal_led(LedIndicator *led)
{
    for (int i = 0; i < led_count; i++) {
        if (leds[i].pub == led)
            return &leds[i];
    }
    return NULL;
}


// LED parallel task
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


void led_init(LedIndicator *led)
{
    gpio_set_direction(led->pin, GPIO_MODE_OUTPUT);
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


// LED functions

void led_on(LedIndicator *led)
{
    LedInternal *internal_led = find_internal_led(led);

    if (internal_led->mode == LED_MODE_ON)
        return;
    ESP_LOGI(TAG_INDICATOR, "LED ON called on pin %d\n", led->pin);

    if (internal_led) internal_led->mode = LED_MODE_ON;
}

void led_off(LedIndicator *led)
{
    LedInternal *internal_led = find_internal_led(led);

    if (internal_led->mode == LED_MODE_OFF)
        return;

    ESP_LOGI(TAG_INDICATOR, "LED OFF called on pin %d\n", led->pin);

    if (internal_led) internal_led->mode = LED_MODE_OFF;
}

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