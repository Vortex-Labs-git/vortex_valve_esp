#ifndef DEVICE_LOG_H
#define DEVICE_LOG_H

/**
 * @file device_log.h
 * @brief Main-process event log sent to the server (hub "Device log").
 *
 * device_log() can be called from any task. The entry is stamped with the
 * current time and queued; the MQTT publish task sends queued entries on
 * the SAME topic as errors:
 *   vortex_device/wifi_valve/<device_id>/error
 * as {"event":"valve_log","device_id","timestamp","level","msg"}.
 * The server tells them apart by "event": "valve_error" = current error
 * state (every 5 s), "valve_log" = one event.
 *
 * Only main events belong here (boot, mode change, valve moved, sensor
 * offline, schedule updated, OTA).
 * The same message repeated within DEVICE_LOG_REPEAT_SEC is dropped.
 */

#include <stdbool.h>

#define DEVICE_LOG_MSG_LEN      96
#define DEVICE_LOG_QUEUE_LEN    16      /* kept while MQTT is offline   */
#define DEVICE_LOG_REPEAT_SEC   3600    /* same text again = dropped    */

typedef struct {
    char level[8];                      /* "info" | "warn"              */
    char timestamp[32];
    char msg[DEVICE_LOG_MSG_LEN];
} DeviceLogEntry;

void device_log_init(void);
void device_log(const char *level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Used by the MQTT publish task: look at the oldest entry, drop it once sent */
bool device_log_peek(DeviceLogEntry *out);
void device_log_drop(void);

#define DLOG_INFO(...)  device_log("info", __VA_ARGS__)
#define DLOG_WARN(...)  device_log("warn", __VA_ARGS__)

#endif
