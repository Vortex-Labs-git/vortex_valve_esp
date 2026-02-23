# Schedule Storage (shedule_storage.c / shedule_storage.h)

## Purpose
Manages persistent storage of valve operation schedules (array of `ScheduleInfo` structs) using ESP-IDF NVS (EEPROM).

## Features
- Saves an array of schedules to NVS as a binary blob.
- Loads schedule data from NVS at boot or on demand.
- Intended for use with remote schedule updates (e.g., via WebSocket controller).

## Main Functions
- `esp_err_t schedule_storage_save(ScheduleInfo *scheList, size_t listSize);`
  - Saves an array of schedules to NVS.
- `esp_err_t schedule_storage_load(ScheduleInfo *scheList, size_t maxListSize, size_t *listSize);`
  - Loads schedules from NVS into a provided array.
  - Returns the number of loaded entries via `listSize`.

## Usage in Main Code
- Example usage is shown in `shedule_storage.c` comments.
- Intended to be called when schedule data is updated (e.g., via WebSocket or UI).
- Not currently called in main or websocket code—consider integrating for full remote schedule management.

## Example
```c
// Save schedule
ScheduleInfo schedule[7] = { ... };
schedule_storage_save(schedule, 7);

// Load schedule
ScheduleInfo loaded[7];
size_t loaded_count;
schedule_storage_load(loaded, 7, &loaded_count);
```

## Note
- Ensure buffer sizes match the number of schedules you expect to store/load.
- Add mutex protection if accessed from multiple tasks.

---
