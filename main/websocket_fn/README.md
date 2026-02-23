# ESP32 Smart Valve Controller: WebSocket Communication Module

---

## Overview
This module implements a secure, thread-safe, and scalable WebSocket communication system for the ESP32 Smart Valve Controller using ESP-IDF. It enables real-time device monitoring, remote control, and WiFi credential updates, supporting both offline and online operation.

---

## System Architecture

### Components
1. **WebSocket Server Layer**
   - Handles HTTP server initialization
   - Manages WebSocket handshake
   - Receives and processes JSON messages
   - Broadcasts responses asynchronously

2. **State & Event Handler Layer**
   - Processes authenticated events
   - Sends device and valve data
   - Updates WiFi credentials
   - Ensures thread-safe shared memory access

---

## Process Flow

### 1. Initialization
- WebSocket server is initialized as part of the HTTP server setup.
- Started when a client connects to ESP32 AP (Access Point mode).
- Handles are maintained for server instance and client connections.

**Key Functions:**
- `start_webserver(void)`
- `stop_webserver(void)`

### 2. Client Connection & Authentication
- Client must authenticate before any command is processed.
- Passkey is compared with `CONFIG_WS_PASSKEY_VALUE`.
- Only authorized clients can send/receive data.

**Example Client Request:**
```json
{
  "event": "request_device_info",
  "passkey": "YOUR_PASSKEY"
}
```

**Device Behavior:**
- If valid: `connection_authorized = true`, sends device info.
- If invalid: connection remains unauthorized, other events ignored.

### 3. Data Reading & Event Handling
- Receives JSON messages from clients via WebSocket.
- Parses and dispatches based on `event` field.
- Validates and processes data, mutex protection for shared resources.

**Key Functions:**
- `ws_handler()`
- `process_message()`
- `websocket_event_handler(...)`

### 4. Data Processing & State Updates
- Device state (valve position, schedule, WiFi credentials, etc.) updated based on client commands.
- Updates are thread-safe using FreeRTOS mutexes.
- Changes persisted to NVS (EEPROM) when necessary.

**Key Functions:**
- `send_device_info(void)`
- `send_device_data(void)`
- `websocket_async_send(void*)`

### 5. Outgoing Communication
- Sends JSON-formatted responses and state updates to clients.
- Asynchronous broadcasting ensures all connected clients receive updates in real time.
- Timestamps and device IDs included for traceability.

**Key Functions:**
- `websocket_async_send(void*)`
- `httpd_queue_work(...)`

---

## WebSocket Message Flow & Examples

### Authentication Flow

**Client Request:**
```json
{
  "event": "request_device_info",
  "passkey": "YOUR_PASSKEY"
}
```

**Device Response:**
```json
{
  "event": "device_info",
  "timestamp": "YYYY-MM-DD HH:MM:SS",
  "device_id": "DEVICE_ID"
}
```

---

### Outgoing Messages (After Authentication)

#### 1. Device Info
Sent immediately after successful authentication.

#### 2. Full Valve State
Sent in response to state requests or when state changes.

```json
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
```

Thread Safety: Uses `valveMutex` to protect shared data.

---

### Supported WebSocket Events (After Authentication)

#### 1. Request Valve State
**Event:** `device_basic_info`

**Client Request:**
```json
{
  "event": "device_basic_info",
  "data": {
    "user_id": "123",
    "device_id": "DEVICE_ID"
  }
}
```
**Device Behavior:**
- Verifies `device_id`.
- If correct, sends full valve state (`send_device_data`).

---

#### 2. Manual Valve Control
**Event:** `set_valve_basic`

**Client Request:**
```json
{
  "event": "set_valve_basic",
  "valve_data": {
    "set_angle": true,
    "angle": 45
  }
}
```
**Device Behavior:**
- Disables schedule and sensor control.
- Validates `set_angle` and updates valve position.
- Updates `serverData` using `serverMutex`.

---

#### 3. Update WiFi Credentials
**Event:** `set_valve_wifi`

**Client Request:**
```json
{
  "event": "set_valve_wifi",
  "wifi_data": {
    "ssid": "YourSSID",
    "password": "YourPassword"
  }
}
```
**Device Behavior:**
- Validates SSID and password.
- Compares with existing credentials.
- If changed:
  - Saves using `wifi_storage_save()`
  - Calls `esp_restart()`
- If unchanged:
  - No action taken

---

### Asynchronous Broadcasts
- Device can broadcast state or error messages to all connected clients using `websocket_async_send()`.
- Example broadcast message:
```json
{
  "event": "valve_error",
  "timestamp": "YYYY-MM-DD HH:MM:SS",
  "device_id": "DEVICE_ID",
  "error": "Overcurrent detected"
}
```

---

## Concurrency Protection
- Mutexes Used:
  - `valveMutex` → Protects valveData
  - `serverMutex` → Protects serverData
- Prevents race conditions between WebSocket, control logic, and sensor update tasks.

---

## Memory Management
- All cJSON objects are deleted after use
- JSON strings are dynamically allocated and freed
- Received WebSocket buffers are freed
- Prevents memory leaks in long-running embedded environment

---

## Error Handling
- Logs errors using ESP_LOGE and ESP_LOGW
- Checks for:
  - Invalid JSON format
  - Missing fields
  - Invalid device ID
  - Incorrect passkey
  - Invalid WiFi structure
  - WebSocket frame receive errors

---

## Configuration (menuconfig)
- `CONFIG_WIFI_VALVE_ID`
- `CONFIG_WS_PASSKEY_VALUE`
- Compiled as:
  - `#define DEVICE_ID CONFIG_WIFI_VALVE_ID`
  - `#define PASSKEY_VALUE CONFIG_WS_PASSKEY_VALUE`

---

## Summary
This module is designed for reliable offline device control and monitoring, featuring:
- Passkey-based authentication
- Structured JSON communication
- Asynchronous WebSocket broadcasting
- Remote valve control
- Remote WiFi reconfiguration
- Safe memory handling
- FreeRTOS mutex protection

---
