# ESP32 MQTT Client: Advanced Documentation

This document provides an in-depth overview of the MQTT client implementation for the ESP32 Smart Valve Controller, focusing on architecture, process flow, and key functions in the `mqtt_fn` folder.

---

## Overview
The MQTT client enables secure, real-time, bidirectional communication between the ESP32 device and remote servers/clouds. It supports both publishing device data and receiving remote commands for control and configuration.

---

## Process Flow


### 1. Initialization Process

#### a. Configuration
- **MQTT Broker URI**: Set via menuconfig (`CONFIG_MQTT_BROKER_URI`).
	- Example: `mqtt://broker.example.com` or `mqtts://ip:port` for TLS.
- **Device ID**: Set via menuconfig (`CONFIG_WIFI_VALVE_ID`).
- **TLS Certificate**: Embedded in firmware (`ca_cert.pem`) for secure connections.

#### b. Topic Structure
- **Base Topic**: `vortex_device/wifi_valve/<DEVICE_ID>`
- **Subtopics**:
	- `state_data`: Publishes full valve state
	- `status`: Publishes online status
	- `error`: Publishes error messages
	- `cmd_data`: Receives remote commands
	- `control_data`: Receives advanced control/schedule updates

#### c. Initialization Steps
1. On WiFi connection, `start_mqtt_client()` is called (see `softap_sta.c`).
2. MQTT client is configured with broker URI, device ID, and TLS certificate.
3. MQTT event handlers are registered for connection, publish, subscribe, and error events.
4. Subscriptions are made to relevant command/control topics.
5. A FreeRTOS task (`mqtt_publish_valve_data_task`) is created to periodically publish device data every 5 seconds.

**Key Functions:**
- `start_mqtt_client(void)`: Initializes and starts the MQTT client, sets up configuration, registers event handlers, subscribes to topics, and starts the periodic publish task.
- `stop_mqtt_client(void)`: Stops the MQTT client and deletes the periodic publish task.

### 2. Connection Management
- Handles connection, reconnection, and disconnection events.
- Maintains connection state (`mqtt_connected`).
- Registers event handlers for publish, subscribe, and error events.

### 3. Publishing Data
- Publishes JSON-formatted messages to specific sub-topics (e.g., `status`, `state_data`).
- Ensures connection is active before publishing.
- Uses `cJSON` for message formatting.

**Key Functions:**
- `mqtt_publish_valve_data(void)`: Publishes current valve/device state.
- `mqtt_publish_message(const char *sub_topic, cJSON *message)`: Publishes a custom JSON message to a sub-topic.

### 4. Receiving and Handling Commands
- Subscribes to command topics (e.g., `cmd_data`, `control_data`).
- Parses incoming JSON messages and dispatches to appropriate handlers.
- Updates device state, schedules, or triggers actions based on received commands.

**Key Functions:**
- `mqtt_handle_cmd_data(const char *data)`: Handles incoming command data.
- `mqtt_handle_control_data(const char *data)`: Handles control-specific data.
- `mqtt_handle_topic(const char *data)`: General topic handler.

### 5. State and Error Reporting
- Publishes device status, state data, and error messages as JSON.
- Uses helper functions to create structured JSON payloads.

**Key Functions:**
- `create_valve_status()`, `create_valve_state_data()`, `create_valve_error()`: Build JSON objects for publishing.

---

## Security
- Uses TLS (with CA certificate) for secure MQTT connections.
- Device ID and credentials are configured via menuconfig.

---

## Integration Points
- MQTT client is started/stopped in main application logic (e.g., when WiFi connects/disconnects).
- Device state is updated and published in response to both local events and remote commands.

---

## Summary
This architecture ensures robust, secure, and scalable MQTT communication for smart device control and monitoring.
