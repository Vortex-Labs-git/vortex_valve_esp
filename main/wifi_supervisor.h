#pragma once
#include <stdbool.h>

/// Start the WiFi link supervisor. Call ONCE, after esp_wifi_start().
void wifi_supervisor_start(void);

/// Live link state: true only when the STA is associated AND holds an IP.
/// Drive the status LED from this — never from a flag set once at connect
/// time, which is what let the LED lie for hours.
bool wifi_link_is_up(void);