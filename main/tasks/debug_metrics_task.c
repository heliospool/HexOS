#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "global_state.h"
#include "nvs_config.h"
#include "work_queue.h"
#include "websocket.h"

#define POLL_INTERVAL_MS    10000
#define CHECK_INTERVAL_MS   1000

static const char *TAG = "debug_metrics";

void debug_metrics_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    // Wait for the system to be ready before doing anything
    vTaskDelay(pdMS_TO_TICKS(5000));

    TickType_t wake_time = xTaskGetTickCount();

    while (1) {
        // Poll every second; only emit logs when debug_log is enabled
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(CHECK_INTERVAL_MS));

        if (!nvs_config_get_bool(NVS_CONFIG_DEBUG_LOG)) {
            continue;
        }

        // Only emit every POLL_INTERVAL_MS even when enabled
        static int64_t last_emit_us = 0;
        int64_t now_us = esp_timer_get_time();
        if ((now_us - last_emit_us) < (POLL_INTERVAL_MS * 1000LL)) {
            continue;
        }
        last_emit_us = now_us;

        SystemModule             *sys  = &GLOBAL_STATE->SYSTEM_MODULE;
        PowerManagementModule    *pwr  = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
        HashrateMonitorModule    *hr   = &GLOBAL_STATE->HASHRATE_MONITOR_MODULE;
        uint8_t                   nchips = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
        uint8_t                   ndoms  = GLOBAL_STATE->DEVICE_CONFIG.family.asic.hash_domains;

        // --- Heap ---
        size_t free_internal  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t free_spiram    = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t min_internal   = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        size_t min_spiram     = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
        size_t largest_block  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

        ESP_LOGI(TAG, "heap: internal=%u min=%u largest=%u | spiram=%u min=%u",
                 (unsigned)free_internal, (unsigned)min_internal, (unsigned)largest_block,
                 (unsigned)free_spiram, (unsigned)min_spiram);

        // --- Power / thermal ---
        ESP_LOGI(TAG, "power: %.2fW  %.3fV  %.3fA (limit=%.1fA OC=%uA)  core=%.3fV",
                 pwr->power, pwr->voltage / 1000.0f, pwr->current / 1000.0f,
                 pwr->current_limit / 1000.0f, (unsigned)pwr->oc_fault_limit,
                 pwr->core_voltage / 1000.0f);

        ESP_LOGI(TAG, "power_fault=0x%04x  overheat=%s",
                 (unsigned)sys->power_fault,
                 sys->overheat_mode ? "YES" : "no");

        ESP_LOGI(TAG, "temp: chip_avg=%.1f°C chip2_avg=%.1f°C  vr=%.1f°C board=%.1f°C  fan=%.0f%%  rpm=%u/%u",
                 pwr->chip_temp_avg, pwr->chip_temp2_avg,
                 pwr->vr_temp, pwr->board_temp,
                 pwr->fan_perc,
                 (unsigned)pwr->fan_rpm, (unsigned)pwr->fan2_rpm);

        // --- ASIC hashrate / error rate ---
        ESP_LOGI(TAG, "hashrate: now=%.1f  1m=%.1f  10m=%.1f  1h=%.1f GH/s  err=%.2f%%",
                 sys->current_hashrate, sys->hashrate_1m,
                 sys->hashrate_10m, sys->hashrate_1h,
                 sys->error_percentage);

        ESP_LOGI(TAG, "asic: init=%s  freq=%.1fMHz (actual=%.1f)  exp=%.1f GH/s",
                 GLOBAL_STATE->ASIC_initalized ? "yes" : "NO",
                 pwr->frequency_value, pwr->actual_frequency,
                 pwr->expected_hashrate);

        // Per-chip frequency and temperature (if available from ASIC register reads)
        if (hr->is_initialized && hr->chip_frequency != NULL) {
            for (uint8_t i = 0; i < nchips; i++) {
                float temp = (hr->chip_temp != NULL) ? hr->chip_temp[i] : 0.0f;
                ESP_LOGI(TAG, "  chip[%u]: freq=%.1fMHz  temp=%.1f°C",
                         (unsigned)i, hr->chip_frequency[i], temp);
            }
        }

        // Per-domain hash counters (total + each domain)
        if (hr->is_initialized && hr->total_measurement != NULL) {
            ESP_LOGI(TAG, "  domain[total]: count=%lu",
                     (unsigned long)hr->total_measurement->value);
        }
        if (hr->is_initialized && hr->domain_measurements != NULL) {
            for (uint8_t chip = 0; chip < nchips; chip++) {
                if (hr->domain_measurements[chip] == NULL) continue;
                for (uint8_t d = 0; d < ndoms; d++) {
                    measurement_t *dm = &hr->domain_measurements[chip][d];
                    ESP_LOGI(TAG, "  chip[%u] domain[%u]: count=%lu  hr=%.1f GH/s",
                             (unsigned)chip, (unsigned)d,
                             (unsigned long)dm->value,
                             dm->hashrate);
                }
            }
        }
        if (hr->is_initialized && hr->error_measurement != NULL) {
            ESP_LOGI(TAG, "  errors: count=%lu",
                     (unsigned long)hr->error_measurement->value);
        }

        // --- Stratum / network ---
        ESP_LOGI(TAG, "stratum: disconnects=%lu tx_err=%lu rx_err=%lu response=%.1fms pool=%s",
                 (unsigned long)sys->stratum_disconnects,
                 (unsigned long)sys->tx_errors,
                 (unsigned long)sys->rx_errors,
                 sys->response_time,
                 sys->pool_connection_info);

        ESP_LOGI(TAG, "shares: accepted=%lu rejected=%lu  last_diff=%.0f  fallback=%s",
                 (unsigned long)sys->shares_accepted,
                 (unsigned long)sys->shares_rejected,
                 sys->last_submitted_diff,
                 sys->is_using_fallback ? "yes" : "no");

        int64_t last_share_sec = (sys->last_share_time > 0)
            ? (now_us - sys->last_share_time) / 1000000 : -1;
        int64_t last_job_sec = (GLOBAL_STATE->ASIC_TASK_MODULE.last_job_sent_us > 0)
            ? (now_us - GLOBAL_STATE->ASIC_TASK_MODULE.last_job_sent_us) / 1000000 : -1;
        ESP_LOGI(TAG, "timing: last_accepted_share=%llds  last_asic_job=%llds",
                 (long long)last_share_sec, (long long)last_job_sec);

        ESP_LOGI(TAG, "network: wifi_disconnects=%lu  connected=%s  status=%s",
                 (unsigned long)sys->wifi_disconnects,
                 sys->is_connected ? "yes" : "NO",
                 sys->wifi_status);

        // --- Work pipeline ---
        ESP_LOGI(TAG, "work: received=%lu  stratum_queue=%d/%d  block_height=%d",
                 (unsigned long)sys->work_received,
                 GLOBAL_STATE->stratum_queue.count, QUEUE_SIZE,
                 GLOBAL_STATE->block_height);

        // --- UART ASIC RX buffer fill ---
        size_t uart_rx_pending = 0;
        uart_get_buffered_data_len(UART_NUM_1, &uart_rx_pending);
        ESP_LOGI(TAG, "uart1: rx_buf=%u bytes", (unsigned)uart_rx_pending);

        // --- WebSocket log queue depth ---
        ESP_LOGI(TAG, "ws_log_queue: %u/%d",
                 (unsigned)log_queue_depth(), MESSAGE_QUEUE_SIZE);
    }
}
