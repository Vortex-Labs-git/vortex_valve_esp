# ESP32 Smart Valve Controller: MQTT Communication Module

---

## Overview
This module implements secure, real-time MQTT communication for the ESP32 Smart Valve Controller. It enables remote monitoring, control, and configuration via a cloud/server using JSON messages over MQTT, with TLS support.

---

## Architecture & Main Components

- **MQTT Client Layer**
  - Handles initialization, connection, reconnection, and disconnection.
  - Publishes device state, status, and error messages.
  - Subscribes to command/control topics for remote actions.
  - Uses TLS with a CA certificate for secure communication.

- **State & Command Handler Layer**
  - Parses incoming JSON commands and updates device state.
  - Handles schedule, control, and WiFi configuration commands.
  - Uses FreeRTOS mutexes for thread-safe updates.

---

## Process Flow

### 1. Initialization & Connection
- MQTT client is started when the ESP32 connects to a WiFi router (`start_mqtt_client()` in `softap_sta.c`).
- Client is stopped when WiFi disconnects (`stop_mqtt_client()`).
- Broker URI, device ID, and credentials are set via menuconfig.

### 2. Periodic Data Publishing
- A FreeRTOS task (`mqtt_publish_valve_data_task`) publishes valve state, status, and error data every 5 seconds.
- Topics follow the pattern:  
  `vortex_device/wifi_valve/<DEVICE_ID>/<sub_topic>`

### 3. Publishing Data
- `mqtt_publish_valve_data()` publishes:
  - `state_data`: Full valve state (angle, limits, etc.)
  - `status`: Online status
  - `error`: Error messages (if any)

### 4. Receiving Commands
- Subscribes to topics like `cmd_data` and `control_data`.
- Handles commands for:
  - Manual valve control
  - Schedule updates
  - WiFi credential updates
  - Sensor threshold configuration

### 5. State & Error Reporting
- Uses helper functions to create structured JSON payloads for all outgoing messages.

---

## Key Functions

- `start_mqtt_client(void)`: Initializes and starts the MQTT client and periodic publish task.
- `stop_mqtt_client(void)`: Stops the MQTT client and deletes the publish task.
- `mqtt_publish_valve_data(void)`: Publishes current valve/device state, status, and error.
- `mqtt_handle_cmd_data(const char *data)`: Handles incoming command data.
- `mqtt_handle_control_data(const char *data)`: Handles advanced control and schedule data.
- `mqtt_handle_topic(const char *data)`: Routes incoming messages based on event type.
- `create_valve_status()`, `create_valve_state_data()`, `create_valve_error()`: Build JSON objects for publishing.

---

## MQTT Message Flow & Examples

### 1. Publishing Device Data (every 5 seconds)

**Topic:** `vortex_device/wifi_valve/<DEVICE_ID>/state_data`
```json
{
  "event": "valve_data",
  "timestamp": "YYYY-MM-DD HH:MM:SS",
  "device_id": "DEVICE_ID",
  "get_controller": { "schedule": true, "sensor": false },
  "get_valvedata": { "angle": 90, "is_open": true, "is_close": false },
  "get_limitdata": { "is_open_limit": true, "open_limit": false, "is_close_limit": true, "close_limit": false },
  "Error": "No Error"
}
```

### 2. Receiving Commands

**Topic:** `vortex_device/wifi_valve/<DEVICE_ID>/cmd_data`
```json
{
  "event": "set_valve_basic",
  "device_id": "DEVICE_ID",
  "set_controller": { "schedule": false, "sensor": false },
  "valve_data": { "set_angle": true, "angle": 45 }
}
```
- Device updates valve state and may publish updated state in response.

**Topic:** `vortex_device/wifi_valve/<DEVICE_ID>/control_data`
```json
{
  "event": "set_valve_wifi",
  "device_id": "DEVICE_ID",
  "wifi_data": { "ssid": "YourSSID", "password": "YourPassword" }
}
```
- Device updates WiFi credentials, saves to NVS, and may restart if changed.

### 3. Error Reporting

**Topic:** `vortex_device/wifi_valve/<DEVICE_ID>/error`
```json
{
  "event": "valve_error",
  "timestamp": "YYYY-MM-DD HH:MM:SS",
  "device_id": "DEVICE_ID",
  "error": "Overcurrent detected"
}
```

---

## Publish Frequency

- Valve state, status, and error data are published every **5 seconds** by the FreeRTOS task `mqtt_publish_valve_data_task`.

---

## Security

- All MQTT communication uses TLS (with CA certificate).
- Device ID and credentials are set via menuconfig.

---

## Integration Points

- MQTT client is managed in `softap_sta.c` based on WiFi connection state.
- Device state is updated and published in response to both local and remote events.

---

## Summary

This module provides robust, secure, and scalable MQTT communication for smart device control and monitoring, with clear message flows, periodic updates, and remote command handling.

---
