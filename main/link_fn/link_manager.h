#ifndef LINK_MANAGER_H
#define LINK_MANAGER_H

#include <stdbool.h>


typedef enum {
    LINK_STATE_SEARCH = 0,      // AP off, looking for the hub
    LINK_STATE_AP_WINDOW,       // AP on, waiting for a phone, still probing the hub
    LINK_STATE_AP_CLIENT,       // phone connected, stay in AP
    LINK_STATE_LINKED,          // paired with the hub, data flowing
} link_state_t;


void         link_manager_start(void);

link_state_t link_manager_get_state(void);
bool         link_manager_is_linked(void);

void         link_manager_report_send(bool ok);

#endif // LINK_MANAGER_H