#ifndef HUB_STORAGE_H
#define HUB_STORAGE_H

#include "esp_err.h"
#include "global_fn/global_var.h"


esp_err_t hub_storage_load(void);
esp_err_t hub_storage_save(void);
void      hub_storage_restore_default(void);

void      hub_storage_save_channel(uint8_t channel);

#endif /* HUB_STORAGE_H */