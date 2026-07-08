#ifndef OTA_UPDATE_FN_H
#define OTA_UPDATE_FN_H

#include <stdbool.h>

void ota_start(const char *url, const char *version);
void ota_confirm_running_firmware(void);
bool ota_in_progress(void);

#endif