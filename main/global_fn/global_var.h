#ifndef GLOBAL_VAR_H
#define GLOBAL_VAR_H

#include <stdbool.h>
#include <stdint.h>
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
    char open[12];
    char close[12];
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


// Define the structure for hub connection (saved in NVS)
typedef struct {
    uint8_t hub_mac[6];         // hub to connect to (00:00:00:00:00:00 = none)
    uint8_t pair_key[16];       // ESPNOW_PAIR_KEY_LEN
    uint8_t last_channel;       // last channel the hub answered on (0 = unknown)
    bool    set_hub;            // true = set by the phone, false = default
} HubConfig;

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
extern HubConfig hubConfig;
extern GetData valveData;
extern ScheduleInfo loaded_schedule[10];
extern bool loaded_schedule_ctrl;
extern size_t loaded_count;

#endif // GLOBAL_VAR_H