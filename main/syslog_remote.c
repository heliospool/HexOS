#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "syslog_remote.h"

#define SYSLOG_PORT     514
#define SYSLOG_MAX_MSG  1024

static const char *TAG = "syslog_remote";

static char s_host[128]         = {0};
static int  s_sock               = -1;
static struct sockaddr_in s_addr = {0};
static SemaphoreHandle_t s_mutex = NULL;

static void close_socket(void)
{
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
}

static bool resolve_and_open(const char *host_with_port)
{
    close_socket();

    /* Parse optional :port suffix; default to SYSLOG_PORT */
    char bare_host[128];
    uint16_t port = SYSLOG_PORT;

    const char *colon = strrchr(host_with_port, ':');
    if (colon != NULL) {
        size_t host_len = (size_t)(colon - host_with_port);
        if (host_len >= sizeof(bare_host)) host_len = sizeof(bare_host) - 1;
        memcpy(bare_host, host_with_port, host_len);
        bare_host[host_len] = '\0';
        int p = atoi(colon + 1);
        if (p > 0 && p <= 65535) port = (uint16_t)p;
    } else {
        strncpy(bare_host, host_with_port, sizeof(bare_host) - 1);
        bare_host[sizeof(bare_host) - 1] = '\0';
    }

    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM };
    struct addrinfo *res = NULL;
    if (getaddrinfo(bare_host, NULL, &hints, &res) != 0 || res == NULL) {
        fprintf(stderr, "syslog_remote: Cannot resolve host: %s\n", bare_host);
        return false;
    }

    memcpy(&s_addr, res->ai_addr, sizeof(s_addr));
    s_addr.sin_port = htons(port);
    freeaddrinfo(res);

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        fprintf(stderr, "syslog_remote: Failed to open UDP socket\n");
        return false;
    }

    fprintf(stderr, "syslog_remote: Sending to %s:%u\n", bare_host, port);
    return true;
}

void syslog_remote_init(const char *host)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    syslog_remote_set_host(host);
}

void syslog_remote_set_host(const char *host)
{
    if (s_mutex == NULL) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (!host || host[0] == '\0') {
        close_socket();
        s_host[0] = '\0';
        fprintf(stderr, "syslog_remote: disabled\n");
        xSemaphoreGive(s_mutex);
        return;
    }

    fprintf(stderr, "syslog_remote: host set to %s\n", host);
    strncpy(s_host, host, sizeof(s_host) - 1);
    s_host[sizeof(s_host) - 1] = '\0';

    resolve_and_open(s_host);

    xSemaphoreGive(s_mutex);
}

void syslog_remote_send(const char *line)
{
    if (s_mutex == NULL || s_sock < 0 || s_host[0] == '\0') return;
    if (xSemaphoreTake(s_mutex, 0) != pdTRUE) return;

    // Static buffers under mutex — removes 2 KB from the caller's stack.
    static char buf[SYSLOG_MAX_MSG];
    static char clean[SYSLOG_MAX_MSG];

    int len = snprintf(buf, sizeof(buf), "<14>HexOS: %s", line);
    if (len <= 0) { xSemaphoreGive(s_mutex); return; }
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;

    int ci = 0;
    for (int i = 0; i < len && ci < (int)sizeof(clean) - 1; i++) {
        if (buf[i] == '\x1b') {
            while (i < len && buf[i] != 'm') i++;
        } else if (buf[i] == '\n' || buf[i] == '\r') {
            break;
        } else {
            clean[ci++] = buf[i];
        }
    }
    clean[ci] = '\0';

    int ret = sendto(s_sock, clean, ci, 0, (struct sockaddr *)&s_addr, sizeof(s_addr));
    if (ret < 0) {
        fprintf(stderr, "syslog_remote: sendto failed errno %d\n", errno);
    }

    xSemaphoreGive(s_mutex);
}
