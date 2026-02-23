# WiFi Storage (wifi_storage.c / wifi_storage.h)

## Purpose
Manages persistent storage of WiFi credentials (SSID, password, set_wifi flag) using ESP-IDF NVS (EEPROM).

## Features
- Loads WiFi credentials from NVS at every boot.
- If no credentials are found, loads default values from menuconfig.
- Allows credentials to be updated and saved (e.g., via WebSocket server).
- Supports restoring defaults (factory reset or config reset).

## Main Functions
- `esp_err_t wifi_storage_load(void);`
  - Loads credentials from NVS into the global `wifiStaData`.
  - Called during device startup (see `softap_sta.c`).
- `esp_err_t wifi_storage_save(void);`
  - Saves current `wifiStaData` to NVS.
  - Called after WiFi credentials are updated (e.g., via WebSocket).
- `void wifi_storage_restore_default(void);`
  - Restores credentials to menuconfig defaults and saves to NVS.
  - Used for factory reset or config reset.

## Usage in Main Code
- In `softap_sta.c`:
  - On boot: `wifi_storage_load()` is called to initialize WiFi credentials.
  - If config reset is enabled: `wifi_storage_restore_default()` is called.
- In WebSocket server logic:
  - When user updates WiFi credentials, `wifi_storage_save()` is called to persist them.

## Thread Safety
- Access to `wifiStaData` should be protected by mutex if used from multiple tasks.

## Example
```c
// On boot
wifi_storage_load();

// When updating credentials
strncpy(wifiStaData.ssid, new_ssid, sizeof(wifiStaData.ssid));
strncpy(wifiStaData.password, new_pass, sizeof(wifiStaData.password));
wifiStaData.set_wifi = true;
wifi_storage_save();
```

---
