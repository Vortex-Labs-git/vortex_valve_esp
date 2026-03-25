# ESP32 Smart Valve Controller: WebSocket Communication Module

---

## Overview

This module implements a **WebSocket-based communication system** that allows a mobile app to interact with the ESP32 Smart Valve Controller in real time.

It is designed to:

* Establish a WebSocket connection
* Authenticate the client
* Process incoming commands
* Send device state updates
* Broadcast messages to multiple clients

The system operates in a **structured flow**, where each stage is handled by specific functions.

---

## System Flow (End-to-End)

### Step 1: Server Startup

The system begins by starting an HTTP server with WebSocket support.

**Function:**

* `start_webserver()`

**What it does:**

* Initializes the HTTP server
* Registers a WebSocket endpoint (`/ws`)
* Prepares the system to accept client connections

---

### Step 2: Client Connection (Handshake)

When a client connects, a WebSocket handshake occurs.

**Function:**

* `ws_handler()`

**What it does:**

* Detects initial HTTP GET request
* Upgrades the connection to WebSocket
* Confirms that a new client is connected

At this stage:

* The client is connected
* But **NOT yet authorized**

---

### Step 3: Receiving Messages

After connection, all incoming messages are handled through the same entry point.

**Function:**

* `ws_handler()`

**What it does:**

1. Receives incoming WebSocket frames
2. Extracts the message payload
3. Passes the message to:

   * `process_message()`

---

### Step 4: Message Processing & Authentication

**Function:**

* `process_message()`

This is the **central decision-making function**.

---

### 4.1 Before Authentication

Only one event is accepted:

* `request_device_info`

**Process:**

1. Extract passkey from the message
2. Compare with configured value

**If valid:**

* Connection is marked as authorized
* Calls:

  * `send_device_info()`

**If invalid:**

* Request is ignored
* Client remains unauthorized

---

### 4.2 After Authentication

All messages are forwarded to:

**Function:**

* `offline_data()`

---

## Step 5: Event Handling (Core Logic)

**Function:**

* `offline_data()`

This function processes all valid commands from the client.

---

### Event 1: Request Device Data

**Flow:**

1. Validate device ID
2. If correct:

   * Call `send_device_data()`

**Result:**

* Client receives full valve state

---

### Event 2: Valve Control

**Flow:**

1. Read control parameters from message
2. Disable automatic modes (schedule/sensor)
3. Update valve control state

**Result:**

* Internal state is updated
* Control task will act on new values

**Important:**

* No response is sent back

---

### Event 3: WiFi Configuration

**Flow:**

1. Read new SSID and password
2. Compare with stored credentials

**If changed:**

* Save new credentials
* Restart ESP32

**If unchanged:**

* Do nothing

---

## Step 6: Sending Data to Client

### Function: `send_device_info()`

**Purpose:**

* Sends basic device identification

**When used:**

* Immediately after successful authentication

---

### Function: `send_device_data()`

**Purpose:**

* Sends full valve state

**What it includes:**

* Controller status (schedule/sensor)
* Valve position (angle, open/close)
* Limit switch states
* Error message

**Key Behavior:**

* Reads shared data safely
* Sends a consistent snapshot

---

## Step 7: Asynchronous Message Sending

### Function: `websocket_async_send()`

This is the **core transmission function**.

---

### How it works:

1. Retrieves all connected clients
2. Filters only WebSocket connections
3. Sends the same message to all clients

**Key Characteristics:**

* Non-blocking (asynchronous)
* Supports multiple clients
* Automatically frees memory after sending

---

### Why Asynchronous?

Messages are sent using:

* `httpd_queue_work()`

This ensures:

* Safe execution outside the main request context
* No blocking of WebSocket handler
* Better performance and stability

---

## Step 8: Server Shutdown

### Function: `stop_webserver()`

**What it does:**

* Stops the HTTP server
* Closes all connections
* Resets authentication state

---

## Thread Safety (Conceptual)

The system ensures safe data access by:

* Separating **read operations** (device state)
* From **write operations** (control commands)

This prevents:

* Data corruption
* Race conditions between tasks

---

## Error Handling Behavior

The system follows a **silent failure model**:

* Invalid messages → ignored
* Unauthorized access → ignored
* Missing fields → ignored

Errors are:

* Logged internally
* Not sent back to the client

---


## Final Summary

The WebSocket module follows a **clean and predictable pipeline**:

1. Start server
2. Accept connection
3. Receive message
4. Authenticate
5. Route event
6. Process command
7. Send response (if needed)
8. Broadcast updates

