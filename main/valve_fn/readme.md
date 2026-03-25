# Valve Control System for ESP32

## Overview

This project implements an **automated valve control system** using an ESP32 microcontroller. It controls a motorized valve, monitors **limit switches** to detect open/close positions, and provides **LED indicators** for system status. The system ensures safe operation, error reporting, and allows both **manual and automatic control**.

Key features:

* Motorized valve operation (open/close) with PWM speed control
* Dual-limit switch detection for accurate valve positioning
* LED indicators for system and error status
* FreeRTOS-based periodic tasks for reliable real-time operation
* Shared `valveData` structure for safe cross-task data handling

---

## System Architecture

### Components

1. **Motor Control (`valve_motor`)**

   * Drives the valve motor in clockwise (close) and anti-clockwise (open) directions.
   * Uses PWM for speed control via ESP32 LEDC module.
   * Provides `motor_init`, `motor_run_clk`, `motor_run_aclck`, and `motor_stop` functions.

2. **Limit Switches (`limit_switch`)**

   * Detects the physical end positions of the valve.
   * Two switches for each position (A and B) to determine "clicked", "not clicked", or "error".
   * Functions: `limit_switch_init`, `limit_switch_click`.

3. **LED Indicators (`led_indicators`)**

   * Provides visual feedback for system status and errors.
   * Supports ON, OFF, and two blink modes.
   * Managed by a FreeRTOS task that handles all LEDs in parallel.

4. **Valve Process (`valve_process`)**

   * Coordinates motor, limit switches, and LEDs.
   * Provides initialization, test, open, and close functions.
   * Updates shared `valveData` structure for real-time status:

     * `is_open` / `is_close`
     * `close_limit_available` / `open_limit_available`
     * `close_limit_click` / `open_limit_click`
     * `angle` (0° closed, 90° open)
     * `error_msg`

---

## Pin Configuration

| Component     | ESP32 Pin                  |
| ------------- | -------------------------- |
| Motor EN      | `CONFIG_MOTOR_EN_PIN`      |
| Motor IN1     | `CONFIG_MOTOR_IN1_PIN`     |
| Motor IN2     | `CONFIG_MOTOR_IN2_PIN`     |
| Close Limit A | `CONFIG_CLOSE_LIMIT_PIN_A` |
| Close Limit B | `CONFIG_CLOSE_LIMIT_PIN_B` |
| Open Limit A  | `CONFIG_OPEN_LIMIT_PIN_A`  |
| Open Limit B  | `CONFIG_OPEN_LIMIT_PIN_B`  |
| Red LED       | `CONFIG_RED_LED_PIN`       |
| Green LED     | `CONFIG_GREEN_LED_PIN`     |

---

## Initialization Sequence

1. **Motor**

   * Configures GPIO pins for IN1, IN2, EN
   * Initializes LEDC PWM for speed control

2. **Limit Switches**

   * Configures GPIO pins as inputs
   * Pull-up enabled for reliable detection

3. **LED Indicators**

   * Configures GPIO pins as outputs
   * Starts FreeRTOS task for LED management
   * Performs initial blink test

```c
init_valve_system(); // initializes motor, limit switches, LEDs, and blinks LEDs for 1s
```

---

## Operation Flow

### Valve Test (`valve_test`)

* Reads both open and close limit switches
* Updates `valveData` with availability and click status
* Returns an error code if a switch fails

### Open Valve (`motor_open`)

1. Checks current motor state and limit switch availability
2. Rotates motor anti-clockwise until the open limit switch is clicked or timeout occurs
3. Updates `valveData`:

   * `is_open = true`, `is_close = false`, `angle = 90`
4. Updates LEDs:

   * Red LED off on success
   * Red LED on in case of error

### Close Valve (`motor_close`)

1. Checks current motor state and limit switch availability
2. Rotates motor clockwise until the close limit switch is clicked or timeout occurs
3. Updates `valveData`:

   * `is_open = false`, `is_close = true`, `angle = 0`
4. Updates LEDs:

   * Red LED off on success
   * Red LED on in case of error

---

## LED Indicator Management

* Controlled by a **FreeRTOS task** (`led_task`) running every 20 ms
* Supports multiple modes:

  * `LED_MODE_ON` / `LED_MODE_OFF`
  * `LED_MODE_BLINK` (fixed interval)
  * `LED_MODE_BLINK2` (different on/off durations)
* Example usage:

```c
led_on(&greenLED);
led_blink(&redLED, 500); // blink every 500 ms
```

---

## Limit Switch Detection

* Uses **two pins per switch** to detect physical position reliably:

  * A=1, B=0 → clicked
  * A=0, B=1 → not clicked
  * Otherwise → error

* Example usage:

```c
int close_state = limit_switch_click(&closeLimit);
if (close_state == 10) { /* valve fully closed */ }
```

---

## Data Safety

* Shared `valveData` structure is **protected by a FreeRTOS mutex (`valveMutex`)**
* Ensures atomic updates across motor, LED, and limit switch tasks

---

## Error Handling

* Each motor operation checks for:

  * Limit switch availability
  * Timeout (>10 seconds)
* Updates `valveData.error_msg` and sets red LED to indicate fault
* Error codes:

  * `111` / `121`: limit switch unavailable
  * `231` / `331`: motor timeout errors

