# ESP32 Smart Valve Controller  
## WebSocket Offline Communication Module

---

# Overview

This project implements a WebSocket-based offline communication system for an ESP32 Smart Valve Controller using ESP-IDF.

It provides:

- Secure WebSocket authentication using passkey
- Real-time valve state monitoring
- Remote valve control
- WiFi credential update via WebSocket
- Thread-safe shared data handling
- Asynchronous WebSocket broadcasting
- JSON-based communication using cJSON

---

# System Architecture

The system consists of two major components:

1. WebSocket Server Layer  
   - Handles HTTP server initialization
   - Manages WebSocket handshake
   - Receives and processes JSON messages
   - Broadcasts responses asynchronously

2. State & Event Handler Layer  
   - Processes authenticated events
   - Sends device and valve data
   - Updates WiFi credentials
   - Ensures thread-safe shared memory access

---

# Technologies Used

- ESP-IDF
- FreeRTOS (Mutex handling)
- ESP HTTP Server (WebSocket)
- cJSON
- EEPROM storage for WiFi credentials

---

# Authentication Flow

Before processing any command, the client must authenticate.

## Client Request

{
  "event": "request_device_info",
  "passkey": "YOUR_PASSKEY"
}

## Device Behavior

- Compares passkey with CONFIG_WS_PASSKEY_VALUE
- If valid:
  - connection_authorized = true
  - Sends device_info
- If invalid:
  - Connection remains unauthorized
  - Other events are ignored

---

# WebSocket Server

## Start Server

Function: start_webserver()

- Initializes HTTP server
- Enables LRU purge
- Registers WebSocket endpoint:
  URI: /ws
  Method: HTTP_GET
  Type: WebSocket

## Stop Server

Function: stop_webserver()

- Stops HTTP server
- Resets esp_server handle
- Clears authorization state

---

# WebSocket Handler

Function: ws_handler()

Handles:

1. WebSocket Handshake (HTTP GET)
2. Receiving WebSocket frames
3. Parsing JSON payload
4. Calling process_message()

Memory for received payload is dynamically allocated and freed after processing.

---

# Message Processing

Function: process_message()

Steps:

1. Parse JSON using cJSON_Parse()
2. Extract "event" field
3. If not authorized:
   - Only request_device_info allowed
4. If authorized:
   - Route event to offline_data()
5. Free JSON object

---

# Outgoing Messages

## 1. send_device_info()

Sends device identification details.

JSON Format:

{
  "event": "device_info",
  "timestamp": "YYYY-MM-DD HH:MM:SS",
  "device_id": "DEVICE_ID"
}

Used immediately after successful authentication.

---

## 2. send_device_data()

Sends full valve state.

Includes:

- Controller state
- Valve angle
- Limit switch state
- Error message

JSON Format:

{
  "event": "valve_data",
  "timestamp": "YYYY-MM-DD HH:MM:SS",
  "device_id": "DEVICE_ID",
  "get_controller": {
    "schedule": true,
    "sensor": false
  },
  "get_valvedata": {
    "angle": 90,
    "is_open": true,
    "is_close": false
  },
  "get_limitdata": {
    "is_open_limit": true,
    "open_limit": false,
    "is_close_limit": true,
    "close_limit": false
  },
  "Error": "No Error"
}

Thread Safety:
- Copies valveData using valveMutex before building JSON.

---

# Supported WebSocket Events

After authentication, the following events are supported:

---

## 1. device_basic_info

Purpose:
Client requests valve state.

Request:

{
  "event": "device_basic_info",
  "data": {
    "user_id": "123",
    "device_id": "DEVICE_ID"
  }
}

Behavior:

- Verifies device_id
- If correct → calls send_device_data()
- If incorrect → ignored

---

## 2. set_valve_basic

Purpose:
Manually control valve angle.

Request:

{
  "event": "set_valve_basic",
  "valve_data": {
    "set_angle": true,
    "angle": 45
  }
}

Behavior:

- Disables schedule and sensor control
- Validates set_angle
- Updates serverData using serverMutex

---

## 3. set_valve_wifi

Purpose:
Update WiFi credentials remotely.

Request:

{
  "event": "set_valve_wifi",
  "wifi_data": {
    "ssid": "YourSSID",
    "password": "YourPassword"
  }
}

Behavior:

- Validates ssid and password
- Compares with existing credentials
- If changed:
  - Saves using wifi_storage_save()
  - Calls esp_restart()
- If unchanged:
  - No action taken

---

# Asynchronous WebSocket Broadcast

Function: websocket_async_send()

- Retrieves connected clients
- Checks for WebSocket type
- Sends JSON using httpd_ws_send_frame_async()
- Frees dynamically allocated JSON string

Supports up to:
- EXAMPLE_MAX_STA_CONN = 5
- MAX_CLIENTS = 10

---

# Concurrency Protection

Mutexes Used:

- valveMutex → Protects valveData
- serverMutex → Protects serverData

Prevents race conditions between:

- WebSocket task
- Control logic task
- Sensor update task

---

# Memory Management

- All cJSON objects are deleted after use
- JSON strings are dynamically allocated and freed
- Received WebSocket buffers are freed
- Prevents memory leaks in long-running embedded environment

---

# Error Handling

The system logs errors using ESP_LOGE and ESP_LOGW.

Checks include:

- Invalid JSON format
- Missing fields
- Invalid device ID
- Incorrect passkey
- Invalid WiFi structure
- WebSocket frame receive errors

---

# Configuration (menuconfig)

The following values are configured via menuconfig:

- CONFIG_WIFI_VALVE_ID
- CONFIG_WS_PASSKEY_VALUE

These are compiled into:

#define DEVICE_ID CONFIG_WIFI_VALVE_ID
#define PASSKEY_VALUE CONFIG_WS_PASSKEY_VALUE

---

# Summary

This module provides a secure, thread-safe, and scalable WebSocket communication framework for an ESP32 Smart Valve Controller.

Features:

- Passkey-based authentication
- Structured JSON communication
- Asynchronous WebSocket broadcasting
- Remote valve control
- Remote WiFi reconfiguration
- Safe memory handling
- FreeRTOS mutex protection

Designed for reliable offline device control and monitoring.
