# ESP32 WebSocket Server: Advanced Documentation

This document provides an in-depth overview of the WebSocket server implementation for the ESP32 Smart Valve Controller, focusing on the architecture, process flow, and key functions found in the `websocket_fn` folder.

---

## Overview
The WebSocket server enables real-time, bidirectional communication between the ESP32 device and remote clients (such as mobile apps or web dashboards). It is designed for secure, asynchronous, and thread-safe operation, supporting both device control and monitoring.

---

## Process Flow

### 1. Initialization
- The WebSocket server is initialized as part of the HTTP server setup.
- The server is started when a client connects to the ESP32 AP (Access Point mode).
- Handles are maintained for the server instance and client connections.

**Key Functions:**
- `start_webserver(void)`: Starts the HTTP/WebSocket server.
- `stop_webserver(void)`: Stops the server and resets state.

### 2. Client Connection & Authentication
- When a client connects, the server checks for a valid authentication passkey (from menuconfig).
- Only authorized clients can send/receive data.
- Connection state is tracked with flags.

**Key Functions:**
- `connection_authorized`: Boolean flag for client authentication.

### 3. Data Reading & Event Handling
- The server receives JSON messages from clients via WebSocket.
- Each message is parsed and dispatched based on its `event` field (e.g., `device_basic_info`, `set_valve_basic`).
- Data is validated and processed, with mutex protection for shared resources.

**Key Functions:**
- `websocket_event_handler(...)`: Handles incoming WebSocket events and messages.
- `websocket_state_fn.c` functions: Process specific events and update device state.

### 4. Data Processing & State Updates
- Device state (valve position, schedule, WiFi credentials, etc.) is updated based on client commands.
- All updates are thread-safe using FreeRTOS mutexes.
- Changes are persisted to NVS (EEPROM) when necessary.

**Key Functions:**
- `send_device_info(void)`: Sends device identification info.
- `send_device_data(void)`: Sends full valve state.
- `send_error_message(const char*)`: Sends error notifications.
- `websocket_async_send(void*)`: Asynchronously broadcasts JSON messages to all clients.

### 5. Outgoing Communication
- The server sends JSON-formatted responses and state updates to clients.
- Asynchronous broadcasting ensures all connected clients receive updates in real time.
- Timestamps and device IDs are included for traceability.

**Key Functions:**
- `websocket_async_send(void*)`: Core function for sending data to clients.
- `httpd_queue_work(...)`: Used for thread-safe, non-blocking message delivery.

---

## Security & Thread Safety
- Passkey-based authentication for all WebSocket clients.
- FreeRTOS mutexes protect shared data structures during concurrent access.
- Memory management is handled carefully to avoid leaks (allocated JSON strings are freed after use).

---

## Extensibility
- The event-driven architecture allows easy addition of new commands/events.
- JSON schema can be extended for new device features.
- Designed for integration with mobile apps, dashboards, or cloud services.

---

## File Structure
- `websocket_server_fn.c`: Server lifecycle, client management, async broadcast.
- `websocket_state_fn.c`: Event processing, state updates, outgoing message formatting.
- `websocket_server_fn.h` / `websocket_state_fn.h`: Function declarations and shared definitions.

---

## Example Event Flow
1. Client connects to ESP32 AP and opens WebSocket.
2. Client sends authentication message with passkey.
3. Server validates and authorizes connection.
4. Client requests device info or sends control command.
5. Server processes request, updates state, and responds with JSON message.
6. All connected clients receive real-time updates as state changes.

---

## References
- See `websocket_fn/readme.md` for message formats and supported events.
- See `websocket_server_fn.c` and `websocket_state_fn.c` for implementation details.

---

This architecture ensures robust, secure, and scalable WebSocket communication for smart device control and monitoring.
