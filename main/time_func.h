#ifndef TIME_FUNC_H
#define TIME_FUNC_H

#include <stddef.h>

void set_manual_time(void);
void obtain_time(void);
void get_current_timestamp(char *timestamp, size_t timestamp_size);

#endif // TIME_FUNC_H