# Schedule Storage (`schedule_storage.c` / `schedule_storage.h`)

## Overview

This module manages **persistent storage of valve operation schedules** using ESP-IDF NVS (Non-Volatile Storage, i.e., flash memory).  
It is designed to store an array of `ScheduleInfo` structures as a **binary blob** and load it back into the system at boot or on demand.

The goal is to:
- Preserve schedule data across power cycles
- Provide thread-safe access to runtime schedule data

---

## Key Concepts

### 1. NVS (Non-Volatile Storage)

ESP-IDF provides NVS to **store data in flash memory** that persists across device restarts.  
- Data is stored in **namespaces** (like folders)
- Each item has a **key** (like a filename) and a **value**
- NVS supports multiple types: integers, strings, and **binary blobs**

### 2. ScheduleInfo Structure

The `ScheduleInfo` struct represents a single schedule entry:

```c
typedef struct {
    char day[4];      // "Mon", "Tue", etc.
    char start[6];    // "HH:MM" start time
    char end[6];      // "HH:MM" end time
} ScheduleInfo;
````

* Array of `ScheduleInfo` stores multiple daily schedules.
* Stored as **raw memory** in NVS (binary blob).

---

## NVS Configuration in Code

```c
#define SCHEDULE_NVS_NAMESPACE    "schedule_cfg"
#define SCHEDULE_NVS_KEY          "schedule"
static const char *TAG_SCHEDULE = "schedule_storage";
```

* `SCHEDULE_NVS_NAMESPACE`: NVS namespace for schedules
* `SCHEDULE_NVS_KEY`: Key used to store the schedule blob
* `TAG_SCHEDULE`: Logging tag for ESP_LOG messages

---

## Main Functions

### 1. `schedule_storage_save()`

```c
esp_err_t schedule_storage_save(ScheduleInfo *scheList, size_t listSize);
```

**Purpose:** Save an array of schedules to NVS.

**Parameters:**

* `scheList` → pointer to an array of `ScheduleInfo`
* `listSize` → number of elements in the array

**Returns:**

* `ESP_OK` → success
* `ESP_ERR_NVS_*` → NVS-related errors

**How it works:**

1. Open the NVS namespace in **read-write** mode
2. Write the schedule array as a **binary blob**
3. Commit changes to flash
4. Close NVS handle

**Example:**

```c
ScheduleInfo schedule[7] = {
    {"Mon", "08:00", "17:00"},
    {"Tue", "08:00", "17:00"},
};
schedule_storage_save(schedule, 2);
```

**Notes:**

* Stores raw memory. Changing the `ScheduleInfo` struct may break stored data.
* Use mutex if multiple tasks can call this function concurrently.

---

### 2. `schedule_storage_load()`

```c
esp_err_t schedule_storage_load(ScheduleInfo *scheList,
                                size_t maxListSize,
                                size_t *listSize);
```

**Purpose:** Load schedules from NVS into a provided array.

**Parameters:**

* `scheList` → buffer to store loaded schedules
* `maxListSize` → maximum number of `ScheduleInfo` elements buffer can hold
* `listSize` → pointer to store the actual number of loaded entries (optional)

**Returns:**

* `ESP_OK` → success
* `ESP_ERR_NVS_NOT_FOUND` → no schedule stored
* `ESP_ERR_NO_MEM` → buffer too small
* Other NVS errors as applicable

**How it works:**

1. Open NVS namespace in **read-only** mode
2. Query size of stored blob
3. Check if buffer is large enough
4. Clear the buffer (to prevent stale data)
5. Load blob into buffer
6. Return number of loaded entries via `listSize`

**Example:**

```c
ScheduleInfo loaded[7];
size_t loaded_count;
schedule_storage_load(loaded, 7, &loaded_count);
```

**Notes:**

* Always check `listSize` to know how many entries were loaded
* Buffer is cleared before loading to avoid partial updates

---

### 3. `load_eeprom_schedule()`

```c
void load_eeprom_schedule(void);
```

**Purpose:** Bridge between persistent NVS storage and the runtime schedule structure in your system.

**How it works:**

1. Clear temporary buffer `loaded_schedule`
2. Load schedules from NVS using `schedule_storage_load()`
3. If successful:

   * Acquire `serverMutex` (thread-safe access)
   * Copy schedules into `serverControl.schedule_info`
   * Release mutex
4. Log result (success or fallback message)

**Globals Used:**

* `loaded_schedule` → temporary buffer
* `loaded_count` → number of loaded entries
* `serverControl.schedule_info` → runtime schedule data
* `serverMutex` → mutex for safe access

**Example:**

```c
load_eeprom_schedule();
```

**Notes:**

* Should be called at system startup to restore schedules
* Protects shared data with mutex
* If no data is in NVS, the system waits for external updates (MQTT/WebSocket)

---

## Error Handling

| Function                | Common Errors              | Description                      |
| ----------------------- | -------------------------- | -------------------------------- |
| `schedule_storage_save` | `ESP_ERR_NVS_NOT_FOUND`    | Namespace missing or cannot open |
|                         | `ESP_ERR_NVS_WRITE_FAILED` | Flash write failed               |
| `schedule_storage_load` | `ESP_ERR_NVS_NOT_FOUND`    | No schedule stored               |
|                         | `ESP_ERR_NO_MEM`           | Provided buffer too small        |
|                         | Other `ESP_ERR_NVS_*`      | NVS read or internal errors      |

---

## Concurrency Notes

* `load_eeprom_schedule()` uses `serverMutex` to ensure **thread-safe updates**.
* `schedule_storage_save()` **does not internally use a mutex**, so if multiple tasks save at the same time, external synchronization is recommended.

---

## Storage Format Considerations

* Schedules are stored as **raw binary data** (size = `listSize * sizeof(ScheduleInfo)`).
* Changing `ScheduleInfo` struct may make old data unreadable.
* Ensure the buffer sizes in code match the expected number of schedules.

---

## Example Usage

```c
#include "schedule_storage.h"

// 1. Define schedules
ScheduleInfo schedule[7] = {
    {"Mon", "08:00", "17:00"},
    {"Tue", "08:00", "17:00"},
    {"Wed", "08:00", "17:00"}
};

// 2. Save schedules to NVS
esp_err_t save_status = schedule_storage_save(schedule, 3);
if (save_status != ESP_OK) {
    ESP_LOGE("main", "Failed to save schedule: %d", save_status);
}

// 3. Load schedules at runtime
ScheduleInfo loaded[7];
size_t loaded_count;
esp_err_t load_status = schedule_storage_load(loaded, 7, &loaded_count);
if (load_status == ESP_OK) {
    ESP_LOGI("main", "Loaded %d schedule entries", loaded_count);
}

// 4. Load into runtime structure safely
load_eeprom_schedule();
```

---

## Data Flow Diagram

```
[NVS Flash] <---> schedule_storage_load/save <---> [Temporary Buffer]
                          |
                          v
                     (Mutex)
                          |
                          v
                  [serverControl.schedule_info]
```

---

## Summary

This module provides:

* Reliable storage and retrieval of schedules
* Thread-safe integration into runtime structures
* Beginner-friendly functions with clear error handling
* Clear patterns for future expansion (e.g., WebSocket updates)

**Key Takeaways for Beginners:**

1. Use `schedule_storage_save()` to persist schedules.
2. Use `schedule_storage_load()` to read them back.
3. Call `load_eeprom_schedule()` at system startup to initialize runtime schedules.
4. Always handle errors and respect buffer sizes.
5. Be aware of concurrency; use mutex if needed.

---

## References

* [ESP-IDF NVS Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)
* FreeRTOS Mutex Documentation (for thread safety)

