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


#include "global_var.h"




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
    .set_angle          = false,
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
    .schedule_control   = false,
    .sensor_control     = false,
    .set_schedule       = false,
    .sensor_upper_limit = 0,
    .sensor_lower_limit = 0
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
    .error_msg         = ""
};