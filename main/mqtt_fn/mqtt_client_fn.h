#ifndef MQTT_CLIENT_FN_H
#define MQTT_CLIENT_FN_H

void mqtt_publish_valve_data(void);
void mqtt_publish_device_logs(void);

void start_mqtt_client(void);
void stop_mqtt_client(void);

#endif