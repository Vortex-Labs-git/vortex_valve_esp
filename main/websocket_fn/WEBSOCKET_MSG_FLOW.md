# WebSocket Communication: Message Flow and Examples

This section details the message communication process between the client and the ESP32 Smart Valve Controller, structured by process flow. Each step includes example JSON messages and describes the device's behavior.

---

## 1. Authentication Flow

Before any command is processed, the client must authenticate.

### Client Request
```json
{
  "event": "request_device_info",
  "passkey": "YOUR_PASSKEY"
}
```

### Device Behavior
- Compares `passkey` with the configured value.
- If valid:
  - Sets `connection_authorized = true`
  - Sends device info (see below)
- If invalid:
  - Connection remains unauthorized
  - Other events are ignored

---

## 2. Outgoing Messages (After Authentication)

### 2.1. Device Info

Sent immediately after successful authentication.

```json
{
  "event": "device_info",
  "timestamp": "YYYY-MM-DD HH:MM:SS",
  "device_id": "DEVICE_ID"
}
```

---

### 2.2. Full Valve State

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

## 3. Supported WebSocket Events (After Authentication)

### 3.1. Request Valve State

**Event:** `device_basic_info`

#### Client Request
```json
{
  "event": "device_basic_info",
  "data": {
    "user_id": "123",
    "device_id": "DEVICE_ID"
  }
}
```

#### Device Behavior
- Verifies `device_id`.
- If correct, sends full valve state (`send_device_data`).

---

### 3.2. Manual Valve Control

**Event:** `set_valve_basic`

#### Client Request
```json
{
  "event": "set_valve_basic",
  "valve_data": {
    "set_angle": true,
    "angle": 45
  }
}
```

#### Device Behavior
- Disables schedule and sensor control.
- Validates `set_angle` and updates valve position.
- Updates `serverData` using `serverMutex`.

---

### 3.3. Update WiFi Credentials

**Event:** `set_valve_wifi`

#### Client Request
```json
{
  "event": "set_valve_wifi",
  "wifi_data": {
    "ssid": "YourSSID",
    "password": "YourPassword"
  }
}
```

#### Device Behavior
- Validates SSID and password.
- Compares with existing credentials.
- If changed:
  - Saves using `wifi_storage_save()`
  - Calls `esp_restart()`
- If unchanged:
  - No action taken

---

## 4. Asynchronous Broadcasts

- The device can broadcast state or error messages to all connected clients using `websocket_async_send()`.
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

## 5. Error Handling

- Invalid JSON, missing fields, or unauthorized access result in error logs and ignored requests.
- All errors are logged using ESP_LOGE/ESP_LOGW for debugging.

---

This structured flow ensures secure, reliable, and clear communication between the client and the ESP32 device, supporting robust remote control and monitoring.
