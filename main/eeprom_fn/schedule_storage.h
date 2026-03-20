#ifndef SCHEDULE_STORAGE_H
#define SCHEDULE_STORAGE_H

#include "global_var.h"

esp_err_t schedule_storage_load(ScheduleInfo *scheList,
                                size_t maxListSize,
                                size_t *listSize);
esp_err_t schedule_storage_save(ScheduleInfo *scheList,
                                size_t listSize);

void load_eeprom_schedule();


#endif /* WSCHEDULE_STORAGE_H */