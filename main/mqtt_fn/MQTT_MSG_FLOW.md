# MQTT Communication: Message Flow and Examples

This document details the MQTT message communication process for the ESP32 Smart Valve Controller, structured by process flow. Each step includes example JSON messages and describes device/server behavior.

---

## 1. Publishing Device Data

### Example: Publish Valve State
**Topic:** `vortex_device/wifi_valve/<DEVICE_ID>/state_data`
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

---

## 2. Receiving Commands

### Example: Command Data
**Topic:** `vortex_device/wifi_valve/<DEVICE_ID>/cmd_data`
```json
{
  "event": "set_valve_basic",
  "device_id": "DEVICE_ID",
  "set_controller": {
    "schedule": false,
    "sensor": false
  },
  "valve_data": {
    "set_angle": true,
    "angle": 45
  }
}
```

**Device Behavior:**
- Parses command and updates valve state accordingly.
- May publish updated state in response.

---

### Example: Control Data
**Topic:** `vortex_device/wifi_valve/<DEVICE_ID>/control_data`
```json
{
  "event": "set_valve_wifi",
  "device_id": "DEVICE_ID",
  "wifi_data": {
    "ssid": "YourSSID",
    "password": "YourPassword"
  }
}
```

**Device Behavior:**
- Updates WiFi credentials and saves to NVS.
- May trigger device restart if credentials change.

---

## 3. Error Reporting

### Example: Error Message
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

## 4. OTA and Other Events
- Additional events (e.g., OTA updates) can be handled using similar JSON structures and topic conventions.

---

This structured flow ensures secure, reliable, and clear MQTT communication between the device and remote servers/clouds.
