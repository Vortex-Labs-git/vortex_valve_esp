#include "esp_log.h"
#include "cJSON.h"

#include "websocket_server_fn.h"
#include "websocket_state_fn.h"


/*---------------------------------------------------------------
 * Configuration
 *--------------------------------------------------------------*/

// Maximum number of connected WiFi stations
#define EXAMPLE_MAX_STA_CONN 5
#define MAX_CLIENTS 10

// Flag to indicate whether client is authorized
bool connection_authorized = false;
static const char *TAG_WEBSERVER = "WEB SERVER";

// HTTP server handle
httpd_handle_t esp_server = NULL;


/*===============================================================
 *                STOP WEB SERVER
 *==============================================================*/

/**
 * @brief Stops the running web server and resets state.
 */
void stop_webserver(void)
{
    if (esp_server) {
        httpd_stop(esp_server);
        esp_server = NULL;
        connection_authorized = false;
    }
}


/*===============================================================
 *          ASYNCHRONOUS WEBSOCKET BROADCAST FUNCTION
 *==============================================================*/

/**
 * @brief Send JSON message asynchronously to all connected
 *        WebSocket clients.
 * 
 * @param arg Pointer to dynamically allocated JSON string.
 *            Memory will be freed inside this function.
 */
void websocket_async_send(void *arg) 
{
    char *json_string = (char *)arg;
    
    // Get list of connected clients
    size_t fds = EXAMPLE_MAX_STA_CONN;
    int client_fds[EXAMPLE_MAX_STA_CONN] = {0};
    
    esp_err_t ret = httpd_get_client_list(esp_server, &fds, client_fds);

    if (ret == ESP_OK) {
        for (int i = 0; i < fds; i++) {
            int client_fd = client_fds[i];

            // Only send if this socket is a WebSocket
            // (Standard HTTP requests might be in this list too, but usually transient)
            if (httpd_ws_get_fd_info(esp_server, client_fd) == HTTPD_WS_CLIENT_WEBSOCKET) {
                httpd_ws_frame_t ws_pkt;
                memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
                ws_pkt.payload = (uint8_t *)json_string;
                ws_pkt.len = strlen(json_string);
                ws_pkt.type = HTTPD_WS_TYPE_TEXT;

                httpd_ws_send_frame_async(esp_server, client_fd, &ws_pkt);
            }
        }
    }
    
    // Free the memory allocated in send_device_info_json
    free(json_string);
}





/*===============================================================
 *                WEBSOCKET REQUEST HANDLER
 *==============================================================*/

/**
 * @brief WebSocket handler function
 * 
 * Handles:
 *  - Initial WebSocket handshake (HTTP GET)
 *  - Incoming WebSocket messages (TEXT frames)
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    /*----------------- WebSocket Handshake -----------------*/
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG_WEBSERVER, "Handshake done, the new connection was opened");
        return ESP_OK;
    }

    /*----------------- Receive WebSocket Frame -----------------*/

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // Get the length of the data
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_WEBSERVER, "httpd_ws_recv_frame failed to get frame len: %d", ret);
        return ret;
    }

    // Allocate memory for the payload, if data available
    if (ws_pkt.len) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG_WEBSERVER, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        
        // Receive the actual data
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret == ESP_OK) {
            // json msg decode
            process_message((char*)ws_pkt.payload, &connection_authorized);
        }
        
        ESP_LOGI(TAG_WEBSERVER, "Got packet with message: %s", ws_pkt.payload);
    }
    
    if (buf) free(buf);
    return ret;
}




/*===============================================================
 *                START WEB SERVER
 *==============================================================*/

/**
 * @brief Initialize and start HTTP + WebSocket server.
 * 
 * Registers:
 *    URI: /ws
 *    Type: WebSocket
 */
httpd_handle_t start_webserver(void)
{
    if (esp_server != NULL) {
        ESP_LOGW(TAG_WEBSERVER, "Webserver already running");
        return esp_server;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true; // Auto close old connections if full

    // Start the httpd server
    ESP_LOGI(TAG_WEBSERVER, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&esp_server, &config) == ESP_OK) {
        
        // Registering the WS handler
        ESP_LOGI(TAG_WEBSERVER, "Registering URI handlers");
        httpd_uri_t ws = {
            .uri        = "/ws",
            .method     = HTTP_GET,
            .handler    = ws_handler,
            .user_ctx   = NULL,
            .is_websocket = true
        };
        httpd_register_uri_handler(esp_server, &ws);
        return esp_server;
    }

    ESP_LOGI(TAG_WEBSERVER, "Error starting server!");
    return NULL;
}


