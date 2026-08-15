#ifndef GLOBAL_VAR_H
#define GLOBAL_VAR_H

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern SemaphoreHandle_t valveMutex;
extern SemaphoreHandle_t scheduleMutex;
extern SemaphoreHandle_t serverMutex;



// Define the structure for device data
typedef struct {
    char device_id[32];
    char ap_ssid[64];
} DeviceIdentity;


// Define the structure for set_data
typedef struct {
    bool schedule_control;
    bool sensor_control;
    bool user_control;
    int angle;
} SetData;


// Define the structure for set_control

typedef struct {
    char day[16];
    char open[8];
    char close[8];
    int  angle;    
} ScheduleInfo;
typedef struct {
    int low;                 /* "0-30"  -> low=0  */
    int high;                /*         -> high=30 */
    int angle;               /* value   -> target angle */
} SensorRule;
typedef struct {
    char  unit_id[16];
    char  sensor_id[16];
    char  sensor_name[32];
    char  last_seen[32];
    char  sensor_type[24];
    float sensor_value;      /* arrives as string "45" -> 45.0 */
} SensorData;
typedef struct {
    ScheduleInfo schedule_info[10];
    int          schedule_count;

    SensorRule   sensor_rules[10];
    int          sensor_rule_count;

    SensorData   sensor_data;
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
    float encoder_value;
    float close_limit_encode;
    float open_limit_encode;
    char error_msg[100];
} GetData;


// Declare the global variables
extern DeviceIdentity deviceIdentity;
extern SetData serverData;
extern SetControl serverControl;
extern GetWifi wifiStaData;
extern GetData valveData;
extern ScheduleInfo loaded_schedule[10];
extern bool loaded_schedule_ctrl;
extern size_t loaded_count;

#endif // GLOBAL_VAR_H