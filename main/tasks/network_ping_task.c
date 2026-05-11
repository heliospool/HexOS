#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "lwip/netdb.h"
#include "ping/ping_sock.h"

#include "global_state.h"
#include "network_ping_task.h"

#define PING_INTERVAL_MS    30000
#define PING_TIMEOUT_MS     3000
#define PING_COUNT          3

static const char * TAG = "network_ping";

typedef struct {
    float result_ms;        /* average RTT, or -1.0f if all timed out */
    uint32_t reply_count;
    SemaphoreHandle_t done;
} ping_result_t;

static void on_ping_success(esp_ping_handle_t hdl, void * args)
{
    uint32_t elapsed_ms;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_ms, sizeof(elapsed_ms));
    ping_result_t * r = (ping_result_t *) args;
    r->reply_count++;
    /* incremental true mean: mean_n = mean_{n-1} + (x_n - mean_{n-1}) / n */
    r->result_ms += ((float) elapsed_ms - r->result_ms) / (float) r->reply_count;
}

static void on_ping_timeout(esp_ping_handle_t hdl, void * args)
{
    /* individual packet timed out — result stays -1 if none succeed */
}

static void on_ping_end(esp_ping_handle_t hdl, void * args)
{
    ping_result_t * r = (ping_result_t *) args;
    xSemaphoreGive(r->done);
}

/**
 * Resolve host and send PING_COUNT ICMP echo requests.
 * Returns average RTT in ms, or -1.0f if unreachable / DNS failed.
 */
static float icmp_ping(const char * host)
{
    if (host == NULL) return -1.0f;

    struct addrinfo hints = {
        .ai_family   = AF_INET,
        .ai_socktype = SOCK_RAW,
        .ai_flags    = AI_NUMERICSERV,
    };
    struct addrinfo * res = NULL;
    if (esp_getaddrinfo(host, NULL, &hints, &res) != 0 || res == NULL) {
        ESP_LOGD(TAG, "DNS failed for %s", host);
        return -1.0f;
    }

    struct sockaddr_in * sa = (struct sockaddr_in *) res->ai_addr;
    ip_addr_t target;
    memset(&target, 0, sizeof(target));
    target.type = IPADDR_TYPE_V4;
    target.u_addr.ip4.addr = sa->sin_addr.s_addr;
    freeaddrinfo(res);

    ping_result_t pr = { .result_ms = -1.0f, .reply_count = 0 };
    pr.done = xSemaphoreCreateBinary();
    if (pr.done == NULL) return -1.0f;

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr     = target;
    cfg.count           = PING_COUNT;
    cfg.timeout_ms      = PING_TIMEOUT_MS;
    cfg.interval_ms     = 200;
    cfg.data_size       = 32;
    cfg.task_stack_size = 4096;
    cfg.task_prio       = 1;

    esp_ping_callbacks_t cbs = {
        .cb_args         = &pr,
        .on_ping_success = on_ping_success,
        .on_ping_timeout = on_ping_timeout,
        .on_ping_end     = on_ping_end,
    };

    esp_ping_handle_t hdl = NULL;
    if (esp_ping_new_session(&cfg, &cbs, &hdl) != ESP_OK) {
        vSemaphoreDelete(pr.done);
        return -1.0f;
    }

    esp_ping_start(hdl);

    /* Wait for session to finish (count * (timeout + interval) + margin) */
    uint32_t wait_ms = PING_COUNT * (PING_TIMEOUT_MS + cfg.interval_ms) + 1000;
    xSemaphoreTake(pr.done, pdMS_TO_TICKS(wait_ms));

    esp_ping_stop(hdl);
    esp_ping_delete_session(hdl);
    vSemaphoreDelete(pr.done);

    return pr.result_ms;
}

void network_ping_task(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;
    SystemModule * sys = &GLOBAL_STATE->SYSTEM_MODULE;

    /* Wait for wifi + stratum to come up before first ping */
    vTaskDelay(pdMS_TO_TICKS(10000));

    while (1) {
        const char * host = sys->is_using_fallback ? sys->fallback_pool_url : sys->pool_url;

        float ping = icmp_ping(host);
        sys->network_ping_ms = ping;

        if (ping >= 0.0f) {
            ESP_LOGD(TAG, "pool ICMP ping %.1f ms", ping);
        }

        vTaskDelay(pdMS_TO_TICKS(PING_INTERVAL_MS));
    }
}
