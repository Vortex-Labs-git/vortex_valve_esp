/**
 * @file global_var.c
 * @brief Global shared data structures for system-wide state management
 *
 * This file defines and initializes global structures used across:
 *  - WebSocket server
 *  - MQTT client
 *  - Valve control system
 *  - WiFi configuration manager
 *
 * These structures act as shared state containers between tasks.
 */


#include "global_fn/global_var.h"



/* ======================================================================== */
/* ============================ DEVICE ID DATA ============================ */
/* ======================================================================== */

DeviceIdentity deviceIdentity = {
    .device_id = "",
    .ap_ssid   = ""
};


/* ======================================================================== */
/* ============================ SERVER DATA =============================== */
/* ======================================================================== */

/**
 * @brief Server → Device runtime control flags
 *
 * This structure is typically updated by:
 *  - WebSocket commands
 *  - MQTT messages
 *
 * It represents direct control instructions coming from server/UI.
 */
SetData serverData = {
    .schedule_control   = false,
    .sensor_control     = false,
    .user_control          = false,
    .angle              = 0
};


/* ======================================================================== */
/* =========================== SERVER CONTROL ============================= */
/* ======================================================================== */

/**
 * @brief Configuration parameters received from server
 *
 * Used for updating persistent or operational parameters.
 * This usually represents configuration-level changes
 * rather than immediate control actions.
 */
SetControl serverControl = {
    .schedule_count = 0,
    .sensor_rule_count = 0
};



/* ======================================================================== */
/* ============================= WIFI DATA ================================ */
/* ======================================================================== */

/**
 * @brief Stored WiFi Station credentials
 *
 * Used by WiFi STA initialization.
 * Typically loaded from NVS during boot.
 */
GetWifi wifiStaData = {
    .ssid       = "",
    .password   = "",
    .set_wifi  = false
};


/* ======================================================================== */
/* ============================= VALVE DATA =============================== */
/* ======================================================================== */

/**
 * @brief Current valve status and feedback data
 *
 * This structure represents:
 *  - Real-time valve state
 *  - Limit switch states
 *  - Error messages
 *
 * Typically updated by:
 *  - Valve control task
 *  - Hardware interrupt handlers
 */
GetData valveData = {
    .schedule_control   = false,
    .sensor_control     = false,
    .angle              = 0,
    .is_open            = false,
    .is_close           = false,
    .open_limit_available   = false,
    .open_limit_click       = false,
    .close_limit_available  = false,
    .close_limit_click      = false,
    .encoder_value = 0,
    .close_limit_encode = 0,
    .open_limit_encode = 0,
    .error_msg         = ""
};





ScheduleInfo loaded_schedule[10];
size_t loaded_count = 0;