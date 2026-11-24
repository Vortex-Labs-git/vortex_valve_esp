#ifndef MQTT_CLIENT_FN_H
#define MQTT_CLIENT_FN_H

void mqtt_publish_valve_data(void);
bool mqtt_publish_message(const char *sub_topic, cJSON *message);
bool mqtt_is_connected(void);

void start_mqtt_client(void);
void stop_mqtt_client(void);

#endif