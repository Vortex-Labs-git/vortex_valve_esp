# ESP32 MQTT Client: Advanced Documentation

This document provides an in-depth overview of the MQTT client implementation for the ESP32 Smart Valve Controller, focusing on architecture, process flow, and key functions in the `mqtt_fn` folder.

---

## Overview

The MQTT client enables secure, real-time, bidirectional communication between the ESP32 device and remote servers/clouds. It supports both publishing device data and receiving remote commands for control and configuration.

---

## Process Flow

### 1. Initialization Process

#### a. Configuration

* **MQTT Broker URI**: Set via menuconfig (`CONFIG_MQTT_BROKER_URI`)

  * Example: `mqtt://broker.example.com` or `mqtts://ip:port` for TLS
* **Device ID**: Set via menuconfig (`CONFIG_WIFI_VALVE_ID`)
* **TLS Certificate**: Embedded in firmware (`ca_cert.pem`) for secure connections

#### b. Topic Structure

* **Base Topic**:
  `vortex_device/wifi_valve/<DEVICE_ID>`

* **Subtopics**:

  * `state_data` → Publishes full valve state
  * `status` → Publishes online status
  * `error` → Publishes error messages
  * `cmd_data` → Receives basic control commands
  * `control_data` → Receives advanced control and configuration

#### c. Initialization Steps

1. On WiFi connection, `start_mqtt_client()` is called.
2. MQTT client is configured with:

   * Broker URI
   * Device ID (used as client ID)
   * TLS certificate
3. MQTT event handler is registered for all events.
4. On successful connection:

   * Device subscribes to:

     * `<BASE_TOPIC>/cmd_data`
     * `<BASE_TOPIC>/control_data`
5. A FreeRTOS task (`mqtt_publish_valve_data_task`) is created to periodically publish device data every 5 seconds.

**Key Functions:**

* `start_mqtt_client(void)`
  Initializes and starts the MQTT client, registers event handlers, subscribes to topics, and starts the periodic publish task.

* `stop_mqtt_client(void)`
  Stops the MQTT client and deletes the periodic publish task.

---

## 2. Connection Management

* Handles:

  * MQTT connection
  * Disconnection
  * Automatic reconnection (enabled)
* Maintains connection state using:

  ```c
  static bool mqtt_connected;
  ```
* Subscriptions are triggered upon `MQTT_EVENT_CONNECTED`.

---

## 3. Publishing Data

### Publishing Mechanism

* All messages are:

  * JSON formatted using `cJSON`
  * Published under:

    ```
    vortex_device/wifi_valve/<DEVICE_ID>/<sub_topic>
    ```
* Publish is skipped if MQTT is not connected.

### Periodic Publishing

* A FreeRTOS task runs every 5 seconds:

  ```c
  mqtt_publish_valve_data_task()
  ```
* This publishes:

  * `state_data`
  * `status`
  * `error`

### Key Functions

* `mqtt_publish_valve_data(void)`
  Creates and publishes all device-related data.

* `mqtt_publish_message(const char *sub_topic, cJSON *message)`
  Handles:

  * Topic construction
  * JSON serialization
  * MQTT publish

---

## 4. Receiving and Handling Commands

### Subscription Topics

* `<BASE_TOPIC>/cmd_data`
* `<BASE_TOPIC>/control_data`

### Message Handling Flow

1. MQTT data is received in chunks (supports large payloads).
2. Payload is reassembled into a complete JSON string.
3. Topic is inspected:

   * `/cmd_data` → `mqtt_handle_cmd_data()`
   * `/control_data` → `mqtt_handle_control_data()`
   * Other → `mqtt_handle_topic()`

---

### 4.1 Basic Command Handling (`cmd_data`)

Handled by:

```c
mqtt_handle_cmd_data(const char *data)
```

#### Supported Fields:

* `set_controller`

  * `schedule` (bool)
  * `sensor` (bool)

* `valve_data`

  * `set_angle` (bool)
  * `angle` (int)

#### Behavior:

* Parses JSON
* Updates shared `serverData` using mutex protection

---

### 4.2 Advanced Control Handling (`control_data`)

Handled by:

```c
mqtt_handle_control_data(const char *data)
```

#### Supported Fields:

**1. Controller Settings**

* `set_controllerdata`

  * `schedule` (bool)
  * `sensor` (bool)

**2. Schedule Configuration**

* `set_scheduledata`

  * `set_schedule` (bool)
  * `schedule_info` (array, max 10 entries)

Each entry:

```json
{
  "day": "Monday",
  "open": "08:00",
  "close": "18:00"
}
```

**3. Sensor Limits**

* `set_sensordata`

  * `upper_limit` (int)
  * `lower_limit` (int)

#### Behavior:

* Parses structured JSON
* Updates shared `serverControl` using mutex protection

---

### 4.3 Generic Topic Handler

Handled by:

```c
mqtt_handle_topic(const char *data)
```

* Extracts `"event"` field
* Routes dynamically:

  * `"set_valve_control"` → control handler
  * `"set_valve_basic"` → command handler
* Logs unknown events

---

## 5. State and Error Reporting

### 5.1 Valve Status

Created using:

```c
create_valve_status()
```

Fields:

* `event`: `"valve_status"`
* `timestamp`
* `device_id`
* `status`: `"online"`

---

### 5.2 Valve State Data

Created using:

```c
create_valve_state_data()
```

Includes:

**Controller State**

* `schedule`
* `sensor`

**Valve State**

* `angle`
* `is_open`
* `is_close`

**Limit Switch Data**

* `is_open_limit`
* `open_limit`
* `is_close_limit`
* `close_limit`

---

### 5.3 Valve Error

Created using:

```c
create_valve_error()
```

Fields:

* `event`: `"valve_error"`
* `timestamp`
* `device_id`
* `error` (string from `valveData.error_msg`)

---

## 6. Memory and Data Handling

* Incoming MQTT payloads are:

  * Dynamically allocated
  * Reassembled safely using chunk offsets

* Maximum payload size:

  ```c
  #define MAX_MQTT_PAYLOAD 4096
  ```

* Shared data is protected using:

  * `serverMutex`
  * `valveMutex`

---

## 7. Security

* Uses TLS with embedded CA certificate
* Broker verification enabled
* Client ID = Device ID

---

## 8. Integration Points

* MQTT starts after WiFi connection
* MQTT stops on network shutdown
* Device state:

  * Updated from sensors locally
  * Updated from MQTT commands remotely
* Publishing runs independently via FreeRTOS task

---

## Summary

This implementation provides:

* Reliable bidirectional MQTT communication
* Structured JSON-based protocol
* Thread-safe shared data handling
* Periodic device telemetry
* Scalable topic-based architecture

The system is designed for robustness, modularity, and real-time smart valve control.
