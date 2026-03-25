# 📡 ESP32 WiFi Valve Controller: WiFi State Scenarios

This document explains how the **ESP32 Smart Valve Controller** dynamically manages WiFi connectivity across different real-world scenarios.

The firmware is designed to be:

* 🔄 **Self-recovering**
* 📱 **User-friendly for setup**
* 🌐 **Reliable for cloud operation**

---

## 🌐 WiFi Operating Modes

### 🔹 STA Mode (Station Mode)

* Connects to a WiFi router
* Enables **MQTT communication**
* Disables Access Point (AP)

---

### 🔹 AP Mode (Access Point Mode)

* Creates its own WiFi hotspot
* Used for:

  * Initial setup
  * Configuration
* Runs **Web Server**

---

### 🔹 AP+STA Mode (Hybrid Mode)

* Both AP and STA active simultaneously
* Used for:

  * Provisioning
  * Failover handling
  * Recovery scenarios

---

## 🔁 WiFi Behavior Scenarios

---

### 🚀 1. Device Startup (No Router Available)

**Flow:**

* Device starts in **AP+STA mode**
* Attempts to connect to stored WiFi credentials
* Connection fails → remains in AP+STA

**Behavior:**

* 📡 AP remains ON → user can connect via mobile
* 🔄 Background retries for router connection

✅ **Result:** Device is always accessible for setup

---

### ⚠️ 2. Router Goes Offline During Operation

**Initial State:**

* STA mode active
* MQTT connected

**Event:**

* Router disconnects

**System Response:**

* ❌ MQTT stops
* 🔄 Switches to AP+STA mode
* 📡 AP turns ON
* 🔁 Starts reconnect attempts

✅ **Result:** Device falls back to configuration mode automatically

---

### 🔌 3. Router Comes Back Online

**Initial State:**

* AP+STA mode
* Retrying connection

**Event:**

* Router becomes reachable

**System Response:**

* ✅ Connects to router
* 🔄 Switches to STA mode
* ❌ AP turns OFF
* 🚀 MQTT restarts

✅ **Result:** Seamless recovery to normal operation

---

### 📱 4. Mobile Connected to AP, Router Becomes Available

**Initial State:**

* AP+STA mode
* Mobile connected to AP
* Web server running

**Event:**

* Router becomes available

**System Response:**

* ✅ Connects to router
* 🔄 Switches to STA mode
* ❌ AP turns OFF
* 📱 Mobile gets disconnected
* ❌ Web server stops
* 🚀 MQTT starts

⚠️ **Note:** User may lose connection abruptly

---

### 📵 5. Mobile Connected, Router Still Offline

**State:**

* AP+STA mode
* Router unavailable

**Behavior:**

* 📱 Mobile remains connected
* 🌐 Web server active
* ❌ No MQTT connection

✅ **Result:** Full local configuration available

---

### 👥 6. Multiple Mobile Clients (Router Offline)

**Behavior:**

* Each connection/disconnection triggers AP events
* 📉 When last client disconnects:

  * 🔄 Device resumes router search

✅ **Result:** Efficient resource handling

---

### 🔑 7. WiFi Credentials Updated via Web UI

**Flow:**

* User connects via AP
* Updates SSID/password

**System Response:**

* 💾 Saves credentials
* 🔄 Attempts new connection

**Outcomes:**

* ✅ Success → switches to STA mode
* ❌ Failure → stays in AP+STA mode

✅ **Result:** Safe and flexible reconfiguration

---

### 🛠 8. Temporary AP Mode While Connected to Router

**Optional Feature:**

* Device can enable AP while in STA mode

**Use Case:**

* Local configuration without disconnecting from cloud

**Behavior:**

* AP+STA enabled temporarily
* Returns to STA after configuration

✅ **Result:** Advanced flexibility for maintenance

---

## 🧠 Key Design Principles

### 🔄 Automatic Recovery

* Always retries router connection
* Never gets stuck offline

---

### 📱 User Accessibility

* AP always available when needed
* No manual reset required

---

### ⚡ Efficient Resource Switching

| Feature     | AP Mode | STA Mode |
| ----------- | ------- | -------- |
| Web Server  | ✅       | ❌        |
| MQTT Client | ❌       | ✅        |

---

### 🔒 Conflict Avoidance

* MQTT and Web Server do **not run simultaneously**
* Prevents:

  * Memory issues
  * Network conflicts

---

## 🧭 State Transition Summary

```
          +------------------+
          |   AP + STA Mode  |
          | (Startup / Fail) |
          +--------+---------+
                   |
        Router Found ✅
                   |
                   v
          +------------------+
          |     STA Mode     |
          |   (MQTT Active)  |
          +--------+---------+
                   |
        Router Lost ❌
                   |
                   v
          +------------------+
          |   AP + STA Mode  |
          | (Recovery Mode)  |
          +------------------+
```
