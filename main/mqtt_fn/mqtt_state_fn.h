#ifndef MQTT_STATE_FN_H
#define MQTT_STATE_FN_H

#include "cJSON.h"

void mqtt_handle_cmd_data(const char *data);
void mqtt_handle_control_data(const char *data);
void mqtt_handle_topic(const char *data);

cJSON* create_valve_status();
cJSON* create_valve_state_data();
cJSON* create_valve_error();

#endif