/**
 * @file webui_server.c
 * @brief Browser configuration UI served from the ESP32 SoftAP
 *
 * Serves one embedded HTML page at GET /. The page is self-contained
 * (inline CSS + JS, no external resources — there is no internet on the AP)
 * and drives the device entirely through the EXISTING WebSocket endpoint
 * at /ws, using the same JSON events the mobile app sends:
 *
 *      request_device_info     -> passkey authentication
 *      device_basic_info       -> poll valve status
 *      set_valve_basic         -> open / close / set angle
 *      set_valve_wifi          -> WiFi provisioning (device restarts)
 *      get_motor_calibration   -> read pot / limit values
 *      set_motor_calibration   -> store pot end-stops
 *      set_motor_rotation      -> jog the motor while calibrating
 *
 * Because authorization is per-socket (see websocket_server_fn.c), a browser
 * and the mobile app can be connected at the same time without interfering.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"

#include "webui_server.h"


static const char *TAG_WEBUI = "WEB UI";

/*
 * Embedded by the build system via EMBED_TXTFILES in main/CMakeLists.txt.
 * EMBED_TXTFILES appends a terminating NUL byte, which is NOT part of the
 * document — subtract it from the length or the browser receives a stray byte.
 */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");


/*===============================================================
 *                  GET /  — serve the page
 *==============================================================*/
static esp_err_t webui_get_handler(httpd_req_t *req)
{
    const size_t len = (size_t)(index_html_end - index_html_start) - 1;

    ESP_LOGI(TAG_WEBUI, "Serving config page (%u bytes)", (unsigned)len);

    httpd_resp_set_type(req, "text/html");
    /* The page ships inside the firmware; never let a stale copy survive OTA. */
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    return httpd_resp_send(req, (const char *)index_html_start, len);
}


/*===============================================================
 *                  REGISTER ON EXISTING SERVER
 *==============================================================*/
esp_err_t webui_register(httpd_handle_t server)
{
    if (server == NULL) {
        ESP_LOGE(TAG_WEBUI, "No server handle, cannot register UI");
        return ESP_ERR_INVALID_ARG;
    }

    httpd_uri_t page = {
        .uri         = "/",
        .method      = HTTP_GET,
        .handler     = webui_get_handler,
        .user_ctx    = NULL,
        .is_websocket = false
    };

    esp_err_t err = httpd_register_uri_handler(server, &page);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WEBUI, "Failed to register '/': %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG_WEBUI, "Config page registered at http://192.168.4.1/");
    }

    return err;
}
