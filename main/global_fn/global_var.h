#ifndef GLOBAL_VAR_H
#define GLOBAL_VAR_H

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern SemaphoreHandle_t valveMutex;
extern SemaphoreHandle_t serverMutex;

// Define the structure for set_data
typedef struct {
    bool schedule_control;
    bool sensor_control;
    bool set_angle;
    int angle;
} SetData;


// Define the structure for set_control
#define DAY_SIZE   16   // Enough for "Wednesday" + null
#define TIME_SIZE   8   // Enough for "HH:MM" + optional seconds + null

typedef struct {
    char day[DAY_SIZE];
    char open[TIME_SIZE];
    char close[TIME_SIZE];
} ScheduleInfo;
typedef struct {
    bool schedule_control;
    bool sensor_control;
    bool set_schedule;
    ScheduleInfo schedule_info[20];
    int sensor_upper_limit;
    int sensor_lower_limit;
} SetControl;


// Define the structure for get_wifi
typedef struct {
    char ssid[32];
    char password[64];
    bool set_wifi;
} GetWifi;

// Define the structure for get_data
typedef struct {
    bool schedule_control;
    bool sensor_control;
    int angle;
    bool is_open;
    bool is_close;
    bool open_limit_available;
    bool open_limit_click;
    bool close_limit_available;
    bool close_limit_click;
    char error_msg[100];
} GetData;


// Declare the global variables
extern SetData serverData;
extern SetControl serverControl;
extern GetWifi wifiStaData;
extern GetData valveData;
extern ScheduleInfo loaded_schedule[10];
extern size_t loaded_count;

#endif // GLOBAL_VAR_H