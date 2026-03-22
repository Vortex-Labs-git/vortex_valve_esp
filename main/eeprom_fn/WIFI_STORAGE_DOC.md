# WiFi Storage (`wifi_storage.c` / `wifi_storage.h`)

## Overview

This module manages **persistent storage of WiFi credentials** using ESP-IDF NVS (Non-Volatile Storage).  
It allows the system to:

- Load saved WiFi credentials at startup
- Save updated credentials from UI or WebSocket
- Restore default credentials from `menuconfig` when necessary

WiFi credentials are stored as a **binary blob** (`GetWifi` structure).

---

## Key Concepts

### 1. NVS (Non-Volatile Storage)

ESP-IDF NVS is flash memory that retains data across power cycles.  
- Organized in **namespaces** (like folders)  
- Each item has a **key** (like a filename) and a **value**  
- Supports types: integers, strings, and binary blobs  

### 2. `GetWifi` Structure

Represents WiFi credentials stored in NVS:

```c
typedef struct {
    char ssid[32];       // WiFi SSID
    char password[64];   // WiFi password
    bool set_wifi;       // true if user-configured, false if default
} GetWifi;
````

**Notes:**

* `set_wifi = true` → user configured WiFi
* `set_wifi = false` → default WiFi

---

## NVS Configuration in Code

```c
#define DEFAULT_WIFI_SSID     CONFIG_ESP_WIFI_STA_SSID
#define DEFAULT_WIFI_PASS     CONFIG_ESP_WIFI_STA_PASSWD
#define WIFI_NVS_NAMESPACE    "wifi_cfg"
#define WIFI_NVS_KEY          "sta"
static const char *TAG_WIFI = "wifi_storage";
```

* `DEFAULT_WIFI_SSID` / `DEFAULT_WIFI_PASS`: default credentials from `menuconfig`
* `WIFI_NVS_NAMESPACE`: namespace for storing WiFi data
* `WIFI_NVS_KEY`: key for WiFi blob
* `TAG_WIFI`: logging tag for ESP_LOG messages

---

## Main Functions

### 1. `wifi_storage_load()`

```c
esp_err_t wifi_storage_load(void);
```

**Purpose:** Load WiFi credentials from NVS into global `wifiStaData`.

**Flow:**

1. Open NVS namespace in **read-only** mode
2. Read binary blob into `wifiStaData`
3. Close NVS handle
4. Log result

**Returns:**

* `ESP_OK` → successful load
* `ESP_ERR_NVS_NOT_FOUND` → no stored credentials
* Other `ESP_ERR_NVS_*` errors if reading fails

**Example:**

```c
if (wifi_storage_load() != ESP_OK) {
    ESP_LOGW("main", "No WiFi stored, using defaults");
}
```

---

### 2. `wifi_storage_save()`

```c
esp_err_t wifi_storage_save(void);
```

**Purpose:** Save `wifiStaData` to NVS as a binary blob.

**Flow:**

1. Validate WiFi data:

   * If `set_wifi == true`, SSID must not be empty
2. Open NVS namespace in **read-write** mode
3. Write `wifiStaData` as blob
4. Commit changes
5. Close NVS handle
6. Log result

**Returns:**

* `ESP_OK` → saved successfully
* `ESP_ERR_INVALID_ARG` → invalid WiFi data (e.g., empty SSID)
* Other `ESP_ERR_NVS_*` errors on failure

**Example:**

```c
wifiStaData.set_wifi = true;
strcpy(wifiStaData.ssid, "MySSID");
strcpy(wifiStaData.password, "MyPass");
wifi_storage_save();
```

**Notes:**

* Commits changes to flash to make them permanent
* Error logging provides details for debugging

---

### 3. `wifi_storage_restore_default()`

```c
void wifi_storage_restore_default(void);
```

**Purpose:** Reset WiFi credentials to **defaults defined in menuconfig**.

**Flow:**

1. Clear `wifiStaData` with `memset()`
2. Copy default SSID and password
3. Set `set_wifi = false` to indicate default configuration
4. Save updated structure to NVS
5. Log action

**Use cases:**

* Factory reset
* Firmware reset triggers
* `CONFIG_ESP_WIFI_STA_MODE_RESET` enabled

**Example:**

```c
wifi_storage_restore_default();
```

**Notes:**

* Always saves to NVS after restoring defaults
* Ensures consistent state between runtime and persistent storage

---

## Error Handling

| Function            | Common Errors           | Description                   |
| ------------------- | ----------------------- | ----------------------------- |
| `wifi_storage_load` | `ESP_ERR_NVS_NOT_FOUND` | No stored WiFi credentials    |
|                     | Other `ESP_ERR_NVS_*`   | NVS read errors               |
| `wifi_storage_save` | `ESP_ERR_INVALID_ARG`   | User-configured SSID is empty |
|                     | Other `ESP_ERR_NVS_*`   | NVS write or commit errors    |

---

## Storage Format Considerations

* WiFi credentials are stored as a **raw binary blob**

  * Size = `sizeof(GetWifi)`
* Changing the `GetWifi` structure may break stored data
* Maximum SSID: 32 characters, maximum password: 64 characters
* `set_wifi` indicates whether credentials are default or user-defined

---

## Example Usage

```c
#include "wifi_storage.h"

// Load WiFi at startup
if (wifi_storage_load() != ESP_OK) {
    ESP_LOGI("main", "Using default WiFi");
}

// User updates WiFi via UI
wifiStaData.set_wifi = true;
strcpy(wifiStaData.ssid, "HomeSSID");
strcpy(wifiStaData.password, "SuperSecret");
wifi_storage_save();

// Factory reset WiFi
wifi_storage_restore_default();
```

---

## Data Flow Diagram

```text
[NVS Flash] 
     │
     ▼
wifi_storage_load/save
     │
     ▼
[wifiStaData global structure] → Used by WiFi manager or runtime tasks
```

---

## Summary

This module provides:

* Persistent WiFi credential storage
* Runtime-global access via `wifiStaData`
* Ability to restore default credentials
* Beginner-friendly API with error logging and validation

**Key Points for Beginners:**

1. `wifi_storage_load()` → call at startup to load saved WiFi
2. `wifi_storage_save()` → call after user updates WiFi
3. `wifi_storage_restore_default()` → resets WiFi to defaults
4. Always handle errors and validate SSID/password
5. Be mindful of concurrent access to `wifiStaData`

---

## References

* [ESP-IDF NVS Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)

