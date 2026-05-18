#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "websocket.h"
#include "http_server.h"
#include "syslog_remote.h"

static const char * TAG = "websocket";

static QueueHandle_t log_queue = NULL;
static int clients[MAX_WEBSOCKET_CLIENTS];
static int active_clients = 0;
static SemaphoreHandle_t clients_mutex = NULL;

int log_to_queue(const char *format, va_list args)
{
    // Format into a stack buffer — no heap allocation needed for stdout + syslog paths.
    // Reserve the last 2 bytes for an optional '\n' and null terminator.
    char stack_buf[512];
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(stack_buf, sizeof(stack_buf) - 1, format, args_copy);
    va_end(args_copy);
    if (len <= 0) return 0;
    if (len >= (int)(sizeof(stack_buf) - 1)) len = (int)(sizeof(stack_buf) - 2);
    if (stack_buf[len - 1] != '\n') {
        stack_buf[len++] = '\n';
    }
    stack_buf[len] = '\0';

    fputs(stack_buf, stdout);
    syslog_remote_send(stack_buf);

    // Heap-allocate and enqueue only when WebSocket clients are connected.
    if (active_clients > 0) {
        char *heap_buf = (char *)malloc(len + 1);
        if (heap_buf != NULL) {
            memcpy(heap_buf, stack_buf, len + 1);
            if (xQueueSendToBack(log_queue, &heap_buf, pdMS_TO_TICKS(100)) != pdPASS) {
                free(heap_buf);
            }
        }
    }

    return len;
}

static esp_err_t add_client(int fd)
{
    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for adding client");
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_FAIL;
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i] == -1) {
            clients[i] = fd;
            active_clients++;
            ESP_LOGI(TAG, "Added WebSocket client, fd: %d, slot: %d", fd, i);
            ret = ESP_OK;
            break;
        }
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Max WebSocket clients reached, cannot add fd: %d", fd);
    }

    xSemaphoreGive(clients_mutex);
    return ret;
}

static void remove_client(int fd)
{
    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for removing client");
        return;
    }

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i] == fd) {
            clients[i] = -1;
            active_clients--;
            ESP_LOGI(TAG, "Removed WebSocket client, fd: %d, slot: %d", fd, i);

            break;
        }
    }

    xSemaphoreGive(clients_mutex);
}

void websocket_close_fn(httpd_handle_t hd, int fd)
{
    ESP_LOGI(TAG, "WebSocket client disconnected, fd: %d", fd);
    remove_client(fd);
    close(fd);
}

UBaseType_t log_queue_depth(void)
{
    if (log_queue == NULL) return 0;
    return uxQueueMessagesWaiting(log_queue);
}

esp_err_t websocket_handler(httpd_req_t *req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    if (req->method == HTTP_GET) {
        if (active_clients >= MAX_WEBSOCKET_CLIENTS) {
            ESP_LOGE(TAG, "Max WebSocket clients reached, rejecting new connection");
            esp_err_t ret = httpd_resp_send_custom_err(req, "429 Too Many Requests", "Max WebSocket clients reached");
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send error response: %s", esp_err_to_name(ret));
            }
            int fd = httpd_req_to_sockfd(req);
            if (fd >= 0) {
                ESP_LOGI(TAG, "Closing fd: %d for rejected connection", fd);
                httpd_sess_trigger_close(req->handle, fd);
            }
            return ret;
        }

        int fd = httpd_req_to_sockfd(req);
        esp_err_t ret = add_client(fd);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Unexpected failure adding client, fd: %d", fd);
            ret = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unexpected failure adding client");
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send error response: %s", esp_err_to_name(ret));
            }
            ESP_LOGI(TAG, "Closing fd: %d for failed client addition", fd);
            httpd_sess_trigger_close(req->handle, fd);
            return ret;
        }
        ESP_LOGI(TAG, "WebSocket handshake successful, fd: %d", fd);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK || ws_pkt.len == 0) {
        ESP_LOGE(TAG, "Failed to get WebSocket frame size: %s", esp_err_to_name(ret));
        remove_client(httpd_req_to_sockfd(req));
        return ret;
    }

    uint8_t *buf = (uint8_t *)calloc(ws_pkt.len, sizeof(uint8_t));
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for WebSocket frame buffer");
        remove_client(httpd_req_to_sockfd(req));
        return ESP_FAIL;
    }

    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket frame receive failed: %s", esp_err_to_name(ret));
        free(buf);
        remove_client(httpd_req_to_sockfd(req));
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "WebSocket close frame received, fd: %d", httpd_req_to_sockfd(req));
        free(buf);
        remove_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    // TODO: Handle incoming packets here

    free(buf);
    return ESP_OK;
}

void websocket_task(void *pvParameters)
{
    ESP_LOGI(TAG, "websocket_task starting");
    httpd_handle_t https_handle = (httpd_handle_t)pvParameters;

    log_queue = xQueueCreateWithCaps(MESSAGE_QUEUE_SIZE, sizeof(char*), MALLOC_CAP_SPIRAM);
    if (log_queue == NULL) {
        ESP_LOGE(TAG, "Error creating queue");
        vTaskDelete(NULL);
        return;
    }

    memset(clients, -1, sizeof(clients));

    clients_mutex = xSemaphoreCreateMutex();
    if (clients_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create clients mutex");
    }

    esp_log_set_vprintf(log_to_queue);

    // Batch multiple log lines into one frame per send interval to avoid
    // flooding the httpd work queue with one httpd_ws_send_frame_async call.
    #define WS_BATCH_MAX (4096)
    #define WS_SEND_INTERVAL_MS (200)

    char *batch = (char *)heap_caps_malloc(WS_BATCH_MAX, MALLOC_CAP_SPIRAM);
    if (batch == NULL) {
        ESP_LOGE(TAG, "Failed to allocate websocket batch buffer");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        if (active_clients == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Collect the first message (block up to the send interval)
        char *message;
        if (xQueueReceive(log_queue, &message, pdMS_TO_TICKS(WS_SEND_INTERVAL_MS)) != pdPASS) {
            continue;
        }

        // Drain available messages into the batch buffer
        size_t batch_len = 0;
        char *oversized = NULL;
        do {
            size_t msg_len = strlen(message);
            if (batch_len + msg_len < WS_BATCH_MAX) {
                memcpy(batch + batch_len, message, msg_len);
                batch_len += msg_len;
                free(message);
                message = NULL;
            } else if (batch_len == 0) {
                // First message alone exceeds the batch buffer; send it as-is
                oversized = message;
                message = NULL;
                break;
            } else {
                // Batch full; leave this message for the next iteration
                free(message);
                message = NULL;
                break;
            }
        } while (xQueueReceive(log_queue, &message, 0) == pdPASS);

        const char *payload = oversized ? oversized : batch;
        size_t payload_len  = oversized ? strlen(oversized) : batch_len;

        if (payload_len == 0) {
            continue;
        }

        // Send one async call per batch instead of one per log line
        for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
            int client_fd = clients[i];
            if (client_fd != -1) {
                httpd_ws_frame_t ws_pkt;
                memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
                ws_pkt.payload = (uint8_t *)payload;
                ws_pkt.len = payload_len;
                ws_pkt.type = HTTPD_WS_TYPE_TEXT;

                if (httpd_ws_send_frame_async(https_handle, client_fd, &ws_pkt) != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to send WebSocket frame to fd: %d", client_fd);
                    remove_client(client_fd);
                }
            }
        }

        if (oversized) {
            free(oversized);
            oversized = NULL;
        }
    }
}
