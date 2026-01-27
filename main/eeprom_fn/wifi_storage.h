#ifndef WIFI_STORAGE_H
#define WIFI_STORAGE_H

#include "global_var.h"


esp_err_t wifi_storage_load(void);
esp_err_t wifi_storage_save(void);
void wifi_storage_restore_default(void);

#endif /* WIFI_STORAGE_H */
