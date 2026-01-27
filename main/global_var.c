#include "global_var.h"





// Initialize the global variable 'serverData'
SetData serverData = {
    .schedule_control   = false,
    .sensor_control     = false,
    .set_angle          = false,
    .angle              = 0
};

// Initialize the global variable 'serverControl'
SetControl serverControl = {
    .schedule_control   = false,
    .sensor_control     = false,
    .set_schedule       = false,
    .sensor_upper_limit = 0,
    .sensor_lower_limit = 0
};




// Initialize the global variable 'wifiStaData'
GetWifi wifiStaData = {
    .ssid       = "",
    .password   = "",
    .set_wifi  = false
};

// Initialize the global variable 'valveData'
GetData valveData = {
    .schedule_control   = false,
    .sensor_control     = false,
    .angle              = 0,
    .is_open            = false,
    .is_close           = false,
    .open_limit_available   = false,
    .open_limit_click       = false,
    .close_limit_available  = false,
    .close_limit_click      = false,
    .error_msg         = ""
};