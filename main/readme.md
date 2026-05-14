# 📡 Smart WiFi Valve Controller (ESP32 + FreeRTOS)

## 📖 Overview

This project implements a **Smart WiFi Valve Control System** using an ESP32 running **FreeRTOS**.

It allows a valve to be controlled via:

* 🌐 WebSocket (local UI via SoftAP)
* ☁️ MQTT (cloud/server control)
* ⏰ Schedule-based automation
* 📡 Sensor-based automation (framework ready)

The system dynamically switches between:

* **Access Point (AP)** → for configuration
* **Station Mode (STA)** → for normal operation with MQTT

---

## ⚙️ Core Features

### 🔁 Smart WiFi Mode

* Starts in **AP + STA mode**
* Connects to router automatically if credentials exist
* Falls back to **AP mode** if connection fails
* Stops router scanning when user connects to AP
* Switches back to STA when router becomes available

---
<!-- 
### 🔧 Valve Control Modes

| Mode     | Description                        |
| -------- | ---------------------------------- |
| Manual   | Direct angle control (0° or 90°)   |
| Schedule | Time-based open/close automation   |
| Sensor   | Placeholder for sensor-based logic |

---

### 🧠 Task-Based Architecture (FreeRTOS)

The system uses multiple FreeRTOS tasks:

| Task                 | Purpose                               |
| -------------------- | ------------------------------------- |
| `valve_sync_process` | Main control loop (manual + schedule) |
| `schedule_save_task` | Saves schedules to NVS                |
| `obtain_time`        | Synchronizes system time              |

---

## 🧩 System Architecture

```
          +---------------------+
          |   Server / Cloud    |
          | (MQTT / WebSocket)  |
          +----------+----------+
                     |
                     v
             +---------------+
             |  serverData   |
             +---------------+
                     |
                     v
        +---------------------------+
        | valve_sync_process Task   |
        +---------------------------+
           |        |        |
           v        v        v
      Motor     valveData   Schedule
     Control     Update      Logic
```

---

## 📂 Key Modules

### 1. `valve_sync_process.c`

#### 🔹 Purpose

Main control engine that:

* Reads commands from `serverData`
* Executes motor actions
* Updates `valveData`
* Handles schedule automation

---

### 🔄 Execution Flow

#### 1. Copy Server Data (Thread-safe)

```c
xSemaphoreTake(serverMutex);
localServerData = serverData;
xSemaphoreGive(serverMutex);
```

---

#### 2. Update Control Flags

```c
valveData.schedule_control = localServerData.schedule_control;
valveData.sensor_control   = localServerData.sensor_control;
```

---

#### 3. Manual Control Logic

Manual control is allowed only when:

* Valve is not busy
* Schedule mode is OFF
* Sensor mode is OFF

```c
if (!valve_busy &&
    !schedule_control &&
    !sensor_control)
```

Actions:

* `0° → motor_close()`
* `90° → motor_open()`

---

#### 4. Error Handling

* Success → update angle
* Failure → store error message

```c
sprintf(valveData.error_msg, "Failed...");
```

---

#### 5. Schedule-Based Automation

* Reads current time
* Compares with stored schedules
* Decides whether valve should be:

  * OPEN (90°)
  * CLOSED (0°)

```c
if (current >= open && current < close)
    should_open = true;
```

---

### 🕒 Time Handling

```c
int parse_time_str(const char *str);
```

Converts:

```
"14:30" → 870 minutes
```

---

### 📅 Schedule Logic

Each schedule contains:

```c
typedef struct {
    char day[16];
    char open[6];
    char close[6];
} ScheduleInfo;
```

Supports:

* Specific day (e.g., "Monday")
* `"Every day"`

---

## 💾 Schedule Persistence

### `schedule_save_task`

#### 🔹 Purpose

* Detect schedule changes
* Save to EEPROM (NVS)
* Prevent unnecessary writes

---

### 🔍 Change Detection

```c
schedules_are_equal(...)
```

Compares:

* Day
* Open time
* Close time

---

### 💾 Saving

```c
schedule_storage_save(schedule_copy, MAX_SCHEDULES);
```

---

### 🔄 After Save

* Updates `loaded_schedule`
* Resets `set_schedule` flag

---

## 🌐 Global Shared Data

Defined in `global_fn/global_var.c`

---

### 🔹 `serverData`

Runtime commands from server:

```c
SetData {
    schedule_control
    sensor_control
    set_angle
    angle
}
```

---

### 🔹 `serverControl`

Configuration data:

```c
SetControl {
    set_schedule
    schedule_info[]
}
```

---

### 🔹 `valveData`

Device feedback:

```c
GetData {
    angle
    error_msg
    limit switches
}
```

---

### 🔹 `loaded_schedule`

* Stored schedule from NVS
* Max: 10 entries

---

## 🔒 Thread Safety

Uses FreeRTOS Mutex:

* `serverMutex` → protects server data
* `valveMutex` → protects valve state

---

## 📶 WiFi System

### Modes:

* **AP Mode**

  * Used for configuration
  * Runs WebSocket server

* **STA Mode**

  * Connects to router
  * Runs MQTT client

---

### 🔁 Smart Switching Logic

| Event                  | Action                 |
| ---------------------- | ---------------------- |
| STA connected          | Disable AP, start MQTT |
| STA disconnected       | Enable AP              |
| AP client connected    | Stop MQTT, start Web   |
| AP client disconnected | Resume STA             |

---

## 🔌 Motor Control

Functions:

```c
motor_open();
motor_close();
```

* Controlled via GPIO
* Returns error codes

---

## 🚦 Safety Mechanism

### `valve_busy`

Prevents:

* Concurrent motor operations
* Conflicting commands

---

## ⏱ Timing

| Task          | Interval  |
| ------------- | --------- |
| Valve Sync    | 1 second  |
| Schedule Save | 5 seconds |

--- -->