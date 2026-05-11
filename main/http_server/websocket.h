#ifndef WEBSOCKET_H_
#define WEBSOCKET_H_

#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define MESSAGE_QUEUE_SIZE (128)
#define MAX_WEBSOCKET_CLIENTS (10)

esp_err_t websocket_handler(httpd_req_t * req);
void websocket_task(void * pvParameters);
void websocket_close_fn(httpd_handle_t hd, int sockfd);
UBaseType_t log_queue_depth(void);

#endif /* WEBSOCKET_H_ */
