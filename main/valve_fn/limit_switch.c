/**
 * @file limit_switch.c
 * @brief Limit switch input handling for ESP32
 *
 * This module provides functions to initialize and read a dual-pin limit switch.
 * The two pins (pinA and pinB) are used to determine the switch state:
 *  - Clicked
 *  - Not clicked
 *  - Error (invalid or undefined state)
 */

#include <stdbool.h>
#include "driver/gpio.h"

#include "limit_switch.h"

/**
 * @brief Initialize GPIO pins for limit switches
 *
 * Configures both pins as inputs with pull-up resistors.
 *
 * @param switches Pointer to LimitSwitches struct containing pin numbers
 */
void limit_switch_init(LimitSwitches *switches) {
    gpio_set_direction(switches->pinA, GPIO_MODE_INPUT);
    gpio_set_pull_mode(switches->pinA, GPIO_PULLUP_ONLY);

    gpio_set_direction(switches->pinB, GPIO_MODE_INPUT);
    gpio_set_pull_mode(switches->pinB, GPIO_PULLUP_ONLY);
}

/**
 * @brief Read the state of the limit switch
 *
 * Determines the switch state based on the logic of pinA and pinB:
 *  - Clicked: pinA=1, pinB=0
 *  - Not clicked: pinA=0, pinB=1
 *  - Error/Invalid: all other combinations
 *
 * @param switches Pointer to LimitSwitches struct containing pin numbers
 * @return int
 *  - 10: Clicked
 *  - 1: Not clicked
 *  - 0: Error / undefined state
 */
int limit_switch_click(LimitSwitches *switches) {
    int state;
    int pinA_state = gpio_get_level(switches->pinA);
    int pinB_state = gpio_get_level(switches->pinB);

    if (pinA_state == 1 && pinB_state == 0) {
        state = 10;  /**< Switch clicked */
    } else if (pinA_state == 0 && pinB_state == 1) {
        state = 1;   /**< Switch not clicked */
    } else {
        state = 0;   /**< Invalid or error state */
    }
    return state;
}