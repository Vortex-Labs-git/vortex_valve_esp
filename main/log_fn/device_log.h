#ifndef DEVICE_LOG_H
#define DEVICE_LOG_H

/**
 * @file device_log.h
 * @brief Device event log sent to the server (hub "Device log").
 *
 * Only important events are logged, each ONCE, at the place it always
 * passes through:
 *   - errors            log_task watches valveData.error_msg
 *   - valve operations  end of motor_set_angle()
 *   - settings saved    eeprom_fn save functions (schedule, schedule mode,
 *                       WiFi, motor calibration)
 *   - restart + reason  device_log_init() at boot
 *   - firmware update   ota_update_fn.c
 *
 * Entries are kept in RTC memory, so they survive a restart / crash
 * (not a power cut) and are sent when MQTT is connected (log_task.c), on
 * the .../error topic as {"event":"valve_log",...,"level","msg"}.
 */

#include <stdbool.h>

#define DEVICE_LOG_MSG_LEN      128
#define DEVICE_LOG_BUF_LEN      20      /* entries kept until sent; oldest dropped when full */
#define DEVICE_LOG_REPEAT_SEC   3600    /* same text again within this time = dropped */

typedef struct {
    char level[8];                      /* "error" | "warn" | "info" */
    char timestamp[32];
    char msg[DEVICE_LOG_MSG_LEN];
} DeviceLogEntry;

/* Call once at boot, after NVS init: restores unsent entries and logs the restart reason */
void device_log_init(void);

void device_log(const char *level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#define DLOG_ERROR(...) device_log("error", __VA_ARGS__)
#define DLOG_WARN(...)  device_log("warn",  __VA_ARGS__)
#define DLOG_INFO(...)  device_log("info",  __VA_ARGS__)

/* Restart with a reason; the reason is logged after the restart */
void device_restart(const char *reason);

/* Used by log_task: oldest unsent entry, removed once it is sent */
bool device_log_peek(DeviceLogEntry *out);
void device_log_drop(void);

#endif
