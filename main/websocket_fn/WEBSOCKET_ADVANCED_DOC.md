# ESP32 WebSocket Server: Internal Process & Function Flow

This document explains how the WebSocket server operates internally on the ESP32, focusing on **execution flow, function roles, and data handling**.

---

# 1. High-Level Flow

The system follows this pipeline:

```
Client → WebSocket → ws_handler → process_message → offline_data → State Update / Response
```

There are **two main paths**:

1. Incoming message handling
2. Outgoing asynchronous response

---

# 2. Server Lifecycle

## 2.1 `start_webserver(void)`

### Responsibility

* Initializes and starts the HTTP server
* Registers WebSocket endpoint `/ws`

### Flow

1. Check if server already running
2. Load default config:

```c
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
```

3. Enable LRU cleanup:

```c
config.lru_purge_enable = true;
```

4. Start server:

```c
httpd_start(&esp_server, &config);
```

5. Register WebSocket handler:

```c
httpd_register_uri_handler(...);
```

### Result

* Server is ready to accept WebSocket connections

---

## 2.2 `stop_webserver(void)`

### Responsibility

* Clean shutdown

### Actions

* Stops HTTP server
* Resets:

```c
esp_server = NULL;
connection_authorized = false;
```

---

# 3. Connection Handling

## 3.1 `ws_handler(httpd_req_t *req)`

This is the **core entry point for all WebSocket activity**.

---

### Case 1: Handshake (Connection Open)

```c
if (req->method == HTTP_GET)
```

### Behavior

* Triggered during WebSocket upgrade
* Logs connection
* No authentication here

---

### Case 2: Data Frame Received

#### Step-by-step:

### 1. Get payload length

```c
httpd_ws_recv_frame(req, &ws_pkt, 0);
```

### 2. Allocate buffer

```c
buf = calloc(1, ws_pkt.len + 1);
```

### 3. Receive payload

```c
httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
```

### 4. Process message

```c
process_message((char*)ws_pkt.payload, &connection_authorized);
```

### 5. Cleanup

```c
free(buf);
```

---

# 4. Message Processing Layer

## 4.1 `process_message(const char *payload, bool *connection_authorized)`

### Responsibility

* Parse JSON
* Handle authentication
* Route events

---

## Step-by-step Flow

### 1. Parse JSON

```c
cJSON_Parse(payload);
```

If fails → exit

---

### 2. Extract event

```c
cJSON_GetObjectItem(json, "event");
```

If missing → exit

---

## 4.2 Authorization Logic

### Case A: Already Authorized

```c
if (*connection_authorized)
```

➡ Forward to:

```c
offline_data(event, json);
```

---

### Case B: Not Authorized

Only allowed event:

```c
"request_device_info"
```

---

### Passkey Validation

```c
strcmp(passkey->valuestring, PASSKEY_VALUE)
```

---

### If Valid

```c
*connection_authorized = true;
send_device_info();
```

---

### If Invalid

* Remains unauthorized
* Message ignored

---

# 5. Event Dispatcher

## 5.1 `offline_data(cJSON *event, cJSON *json)`

### Responsibility

* Handles all post-authentication events
* Acts as a dispatcher

---

## Supported Events

---

## 5.1.1 `device_basic_info`

### Flow

1. Extract:

```c
data → device_id, user_id
```

2. Validate device:

```c
strcmp(device_id, DEVICE_ID)
```

3. If valid:

```c
send_device_data();
```

---

## 5.1.2 `set_valve_basic`

### Flow

1. Extract:

```c
valve_data
```

2. Initialize control:

```c
schedule_control = false;
sensor_control = false;
```

3. Check:

```c
set_angle == true
```

4. Read angle:

```c
angle->valueint
```

5. Update shared state:

```c
xSemaphoreTake(serverMutex);
serverData = localCopy;
xSemaphoreGive(serverMutex);
```

---

## 5.1.3 `set_valve_wifi`

### Flow

1. Extract:

```c
wifi_data → ssid, password
```

2. Validate strings

3. Compare with stored credentials

---

### If changed:

```c
strncpy(...)
wifi_storage_save();
esp_restart();
```

---

### If unchanged:

* No action

---

# 6. Outgoing Data Flow

---

## 6.1 `send_device_info()`

### Responsibility

* Send device identification

### Flow

1. Generate timestamp
2. Build JSON
3. Convert to string
4. Send asynchronously:

```c
httpd_queue_work(..., websocket_async_send, json_string);
```

---

## 6.2 `send_device_data()`

### Responsibility

* Send full valve state

---

### Step-by-step

1. Copy shared data safely:

```c
xSemaphoreTake(valveMutex);
localCopy = valveData;
xSemaphoreGive(valveMutex);
```

2. Build JSON structure:

* controller
* valve state
* limit switches
* error

3. Send asynchronously

---

# 7. Asynchronous Transmission

## 7.1 `websocket_async_send(void *arg)`

### Responsibility

* Broadcast message to all clients

---

### Flow

1. Get client list:

```c
httpd_get_client_list()
```

2. Loop through clients

3. Check:

```c
HTTPD_WS_CLIENT_WEBSOCKET
```

4. Send frame:

```c
httpd_ws_send_frame_async()
```

---

### Final Step

```c
free(json_string);
```

---

# 8. Concurrency & Data Safety

## Mutex Usage

### Valve Data

```c
valveMutex
```

### Control Data

```c
serverMutex
```

---

## Strategy

* Copy before use
* Lock only when necessary
* Avoid blocking WebSocket task

---

# 9. Important Behavioral Notes

## 9.1 Single Authorization State

```c
bool connection_authorized
```

* Shared globally
* Not per-client
* Suitable for AP mode (single user assumption)

---

## 9.2 No Response Feedback

* No ACK messages
* No error responses
* Only logs are generated

---

## 9.3 Event-Driven Design

* All logic depends on:

```c
"event"
```

* Easy to extend

---

# 10. Complete Execution Flow (Simplified)

```
1. start_webserver()
2. Client connects → ws_handler (GET)
3. Client sends JSON
4. ws_handler → process_message()

5. If not authorized:
      → validate passkey
      → send_device_info()

6. If authorized:
      → offline_data()

7. Event handled:
      → update state OR send response

8. Response:
      → httpd_queue_work()
      → websocket_async_send()
      → client receives data
```
