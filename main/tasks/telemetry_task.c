#include "telemetry_task.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "global_state.h"
#include "nvs_config.h"

static const char *TAG = "telemetry";

#define TELEMETRY_PING_URL "https://updates.heliospool.com/hexos/ping"
#define TELEMETRY_INTERVAL_MS (24ULL * 60 * 60 * 1000)

/* Generate a random 128-bit UUID (version 4) and format as lowercase hex string.
 * Output buf must be at least 37 bytes. */
static void generate_uuid_v4(char *buf)
{
    uint32_t r[4];
    for (int i = 0; i < 4; i++) r[i] = esp_random();

    /* Set version 4 and variant bits */
    r[1] = (r[1] & 0xffff0fffU) | 0x00004000U; /* version 4 */
    r[2] = (r[2] & 0x3fffffffU) | 0x80000000U; /* variant 10xx */

    snprintf(buf, 37,
        "%08" PRIx32 "-%04" PRIx32 "-%04" PRIx32 "-%04" PRIx32 "-%04" PRIx32 "%08" PRIx32,
        r[0],
        r[1] >> 16, r[1] & 0xffff,
        r[2] >> 16, r[2] & 0xffff,
        r[3]);
}

/* Ensure device_id exists in NVS; generate one if not. */
static void ensure_device_id(void)
{
    char *existing = nvs_config_get_string(NVS_CONFIG_TELEMETRY_DEVICE_ID, NULL);
    if (existing == NULL || existing[0] == '\0') {
        char uuid[37];
        generate_uuid_v4(uuid);
        nvs_config_set_string(NVS_CONFIG_TELEMETRY_DEVICE_ID, uuid);
        ESP_LOGI(TAG, "Generated new device ID: %s", uuid);
    }
    free(existing);
}

static void do_ping(GlobalState *GLOBAL_STATE)
{
    if (!nvs_config_get_bool(NVS_CONFIG_TELEMETRY_ENABLED)) {
        ESP_LOGD(TAG, "Telemetry disabled — skipping ping");
        return;
    }

    char *device_id = nvs_config_get_string(NVS_CONFIG_TELEMETRY_DEVICE_ID, NULL);
    if (device_id == NULL || device_id[0] == '\0') {
        free(device_id);
        return;
    }

    char url[256];
    snprintf(url, sizeof(url),
        TELEMETRY_PING_URL "?id=%s&v=%s&board=%s",
        device_id,
        GLOBAL_STATE->SYSTEM_MODULE.version,
        GLOBAL_STATE->DEVICE_CONFIG.board_version);
    free(device_id);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .skip_cert_common_name_check = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Ping sent (HTTP %d)", esp_http_client_get_status_code(client));
    } else {
        ESP_LOGW(TAG, "Ping failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

void telemetry_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    ensure_device_id();

    while (1) {
        do_ping(GLOBAL_STATE);
        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_INTERVAL_MS));
    }
}
