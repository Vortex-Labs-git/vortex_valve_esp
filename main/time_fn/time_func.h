#ifndef TIME_FUNC_H
#define TIME_FUNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void time_module_init(void);

void time_apply_sync(int64_t epoch_utc);
bool time_is_synced(void);

void get_current_timestamp(char *timestamp, size_t timestamp_size);

#endif // TIME_FUNC_H