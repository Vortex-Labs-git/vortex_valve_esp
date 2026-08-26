#ifndef WEBUI_SERVER_H
#define WEBUI_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief Register the browser configuration UI on an existing httpd server.
 *
 * Adds a single URI handler:
 *      GET /   ->  the embedded configuration page
 *
 * The page itself talks to the device over the SAME WebSocket endpoint the
 * mobile app uses (/ws), so no protocol or state code changes are required.
 *
 * @param server  Handle returned by httpd_start() (the existing esp_server)
 * @return ESP_OK on success, otherwise the httpd error code
 */
esp_err_t webui_register(httpd_handle_t server);

#endif // WEBUI_SERVER_H
