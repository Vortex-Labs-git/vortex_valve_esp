# WebSocket Communication: Mobile App ↔ ESP32 (Offline Mode)

This document defines the complete WebSocket communication flow between the Vortex Lab mobile application and the ESP32 Smart Valve Controller when operating in **Access Point (AP) mode (offline mode)**.

It includes:

* Connection establishment
* Authentication
* Device identification
* Data visualization
* Device control
* Wi-Fi configuration

---

# 1. Connection Establishment & Device Identification

## 1.1 User Connection Flow

1. ESP32 operates in **AP mode**.
2. User connects mobile device to ESP32 Wi-Fi network.
3. User opens the **Vortex Lab mobile app**.
4. The app:

   * Reads the current Wi-Fi SSID
   * Verifies it matches a Vortex device pattern
5. If confirmed, the app initiates WebSocket communication.

---

## 1.2 Authentication & Device Info Request

### Client → ESP32

```json
{
  "event": "request_device_info",
  "timestamp": "2025-01-15T10:30:00Z",
  "user_id": "user_id",
  "passkey": "key"
}
```

### ESP32 Behavior

* Parses JSON message
* Validates `passkey`

### If Valid:

* Marks connection as **authorized**
* Sends device information

### If Invalid:

* Connection remains unauthorized
* All further requests are ignored

---

## 1.3 Device Info Response

### ESP32 → Client

```json
{
  "event": "device_info",
  "timestamp": "2025-01-15T10:30:00Z",
  "device_id": "dev0016"
}
```

---

# 2. Valve Details Screen – Data Visualization

## 2.1 Request Valve Data

When user selects a valve device:

### Client → ESP32

```json
{
  "event": "device_basic_info",
  "timestamp": "2025-01-15T10:30:00Z",
  "data": {
    "user_id": "user001",
    "device_id": "dev0016",
    "device_name": "home valve"
  }
}
```

---

## 2.2 ESP32 Behavior

* Verifies `device_id`
* If valid:

  * Reads current valve state
  * Sends full valve data

---

## 2.3 Valve Data Response

### ESP32 → Client

```json
{
  "event": "valve_data",
  "timestamp": "2025-01-15T10:30:00Z",
  "device_id": "dev0016",
  "get_controller": {
    "schedule": true,
    "sensor": false
  },
  "get_valvedata": {
    "angle": 45,
    "is_open": true,
    "is_close": false
  },
  "get_limitdata": {
    "is_open_limit": true,
    "open_limit": false,
    "is_close_limit": false,
    "close_limit": true
  },
  "Error": ""
}
```

---

## 2.4 Notes

* Data is protected using **mutex (`valveMutex`)**
* ESP32 sends a **consistent snapshot** of internal state

---

# 3. Valve Control (Data Editing)

The same Valve Details screen is used for control operations.

---

## 3.1 Supported Controls

* Fully Open
* Fully Close
* Set specific angle
* Update valve nickname (app-level use)

---

## 3.2 Control Request

### Client → ESP32

```json
{
  "event": "set_valve_basic",
  "timestamp": "2025-01-15T10:30:00Z",
  "device_id": "dev0016",
  "set_controller": {
    "schedule": false,
    "sensor": false
  },
  "valve_data": {
    "name": "MainValve01",
    "set_angle": true,
    "angle": 45
  },
  "ota_update": false
}
```

---

## 3.3 ESP32 Behavior

* Disables:

  * Schedule control
  * Sensor control
* Checks `set_angle`:

  * If `true`, updates valve angle
* Updates internal control structure (`serverData`)
  using **`serverMutex`**

### Important Notes

* `name` is not currently used by ESP32 logic (optional field)
* No acknowledgment message is sent back
* No validation feedback is returned to client

---

# 4. Wi-Fi Configuration (STA Mode)

## 4.1 User Action

* User taps **“Change Wi-Fi Connection”**
* Enters SSID and password
* Confirms update

---

## 4.2 Wi-Fi Update Request

### Client → ESP32

```json
{
  "event": "set_valve_wifi",
  "timestamp": "2025-01-15T10:30:00Z",
  "device_id": "dev0016",
  "wifi_data": {
    "ssid": "myNetWork",
    "password": "1234"
  }
}
```

---

## 4.3 ESP32 Behavior

* Validates `ssid` and `password`
* Compares with stored credentials

### If Changed:

* Updates internal storage
* Saves using `wifi_storage_save()`
* Restarts device using `esp_restart()`

### If Unchanged:

* No action taken

---

# 5. Asynchronous Communication

ESP32 supports sending messages to all connected WebSocket clients.

## Mechanism

* Uses:

  * `httpd_queue_work()`
  * `websocket_async_send()`
* Sends JSON messages to all active WebSocket connections

---

## Example Broadcast

```json
{
  "event": "valve_error",
  "timestamp": "2025-01-15T10:30:00Z",
  "device_id": "dev0016",
  "error": "Overcurrent detected"
}
```

---

# 6. Error Handling

## Current Behavior

* Invalid JSON → ignored
* Missing fields → ignored
* Unauthorized requests → ignored
* Invalid `device_id` → ignored

## Logging

* Errors and warnings are logged internally using:

  * `ESP_LOGE`
  * `ESP_LOGW`
  * `ESP_LOGI`

> ⚠️ No error responses are sent to the client (log-only system)

---

# 7. Communication Rules Summary

### Client Must:

1. Connect to ESP32 Wi-Fi (AP mode)
2. Establish WebSocket connection (`/ws`)
3. Authenticate using `request_device_info`
4. Send only supported events

---

### ESP32 Will:

* Reject all messages before authentication
* Process only valid JSON messages
* Respond with structured JSON events
* Broadcast updates when required

---

# ✅ Final Notes

This WebSocket protocol enables:

* Offline device discovery
* Secure (passkey-based) access
* Real-time valve monitoring
* Direct device control
* Wi-Fi provisioning

---
