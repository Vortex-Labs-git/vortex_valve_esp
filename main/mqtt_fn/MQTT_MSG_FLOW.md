# MQTT Communication: Message Flow and Examples

This document details the MQTT message communication process for the ESP32 Smart Valve Controller, structured by process flow. Each step includes example JSON messages and describes device/server behavior.

---

## 1. Publishing Device Status

### Example: Publish Valve Status
**Topic:** `vortex_device/wifi_valve/<DEVICE_ID>/status`
```json
{
	"event": "valve_status",
  "timestamp": "2025-01-15T10:30:00Z",
  "device_id": "dev0016",
  "status": "online",
}
```

---

## 2. Publishing Device Data

### Example: Publish Valve State
**Topic:** `vortex_device/wifi_valve/<DEVICE_ID>/state_data`
```json
{
  "event": "valve_basic_data",
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

## 3. Publishing Device Error

### Example: Publish Valve Error
**Topic:** `vortex_device/wifi_valve/<DEVICE_ID>/error`
```json
{
	  "event": "valve_error",
	  "timestamp": "2025-01-15T10:30:00Z",
   	"device_id": "dev0016",
    "error": "",
	}
```

---


## 4. Receiving Commands

### Example: Command Data
**Topic:** `vortex_device/wifi_valve/<DEVICE_ID>/cmd_data`
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
    "angle": 90
  },
  "ota_update": false
}

```

## 5. Receiving Control Data

### Example: Command Data
**Topic:** `vortex_device/wifi_valve/<DEVICE_ID>/control_data`
```json
{
  "event": "set_valve_control",
  "timestamp": "2025-01-15T10:30:00Z",
  "device_id": "dev0016",
  "set_controllerdata": {
  	"schedule": true,
  	"sensor": false
  },
  "set_scheduledata": {
  	"set_schedule": true,
  	"schedule_info": [
     	{
        "day": "Monday",
      	"open": "08:00",
      	"close": "08.20"
    	},
    	{
        "day": "Monday",
        "open": "18:00",
       	"close": "18.20"
      }
    ]
  },
  "set_sensordata": {
  	"sensor_id": "sensor-01",
  	"upper_limit": 80,
  	"lower_limit": 30
  }
}


```


