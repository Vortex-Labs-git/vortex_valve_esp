# ESP32 WiFi Valve Controller: WiFi State Scenarios

This document describes the dynamic WiFi state transitions and behaviors of the ESP32 Smart Valve Controller firmware. It is intended to help users and developers understand how the device manages its WiFi connectivity and access point (AP) features in real-world situations.

## WiFi Operating Modes
- **STA Mode (Station):** ESP32 connects to a WiFi router. MQTT client is active.
- **AP Mode (Access Point):** ESP32 creates its own WiFi hotspot for configuration. Web server is active.
- **AP+STA Mode:** Both AP and STA are enabled. Used for provisioning and fallback.

## Key Scenarios

### 1. Device Startup, No Router Available
- ESP32 starts in AP+STA mode.
- Attempts to connect to the configured router (STA).
- If connection fails, remains in AP+STA mode and keeps retrying.
- AP is available for mobile devices to connect and configure WiFi.

### 2. Device Connected to Router, Router Goes Offline
- ESP32 is in STA mode, MQTT client is running.
- Router disconnects or goes offline.
- ESP32 stops MQTT client and switches to AP+STA mode.
- AP is enabled for configuration; device retries router connection in the background.

### 3. Router Comes Back Online After Outage
- ESP32 (in AP+STA mode) continues to retry router connection.
- When router is available, ESP32 connects and switches to STA mode.
- AP is disabled, MQTT client restarts.

### 4. Mobile Connects to ESP32 AP, Router Comes Online
- ESP32 is in AP+STA mode; mobile connects to AP, web server starts.
- If router becomes available, ESP32 connects and switches to STA mode.
- AP is disabled, mobile is disconnected, web server stops, MQTT client starts.

### 5. Mobile Connects to AP, Router Remains Offline
- ESP32 stays in AP+STA mode.
- Mobile can configure device, but STA never connects.
- Web server remains available for configuration.

### 6. Multiple Mobiles Connect/Disconnect While Router is Offline
- Each mobile triggers AP connection/disconnection events.
- If all mobiles disconnect, ESP32 resumes searching for router.

### 7. WiFi Credentials Changed via Web Server
- ESP32 receives new credentials from a mobile client via AP.
- Attempts to connect to the new router.
- If successful, switches to STA mode; if not, remains in AP+STA mode.

### 8. User Requests Temporary AP Mode While Connected to Router
- ESP32 can be programmed to enable AP+STA for local configuration even when STA is connected.
- After configuration, returns to STA mode.

## Summary
The ESP32 firmware is designed for robust, user-friendly WiFi management. It automatically switches between AP, STA, and AP+STA modes based on network availability and user actions, ensuring reliable operation and easy configuration in a variety of deployment scenarios.
