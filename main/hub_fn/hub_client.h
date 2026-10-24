#ifndef HUB_CLIENT_H
#define HUB_CLIENT_H

#include "espnow_fn/espnow_proto.h"


void hub_client_start(void);

void hub_publish_valve_data(void);

void hub_client_handle_message(const espnow_hdr_t *hdr, const char *json);

#endif // HUB_CLIENT_H