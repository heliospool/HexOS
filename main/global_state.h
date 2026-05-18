#ifndef GLOBAL_STATE_H_
#define GLOBAL_STATE_H_

#include <stdbool.h>
#include <stdint.h>
#include "asic_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "power_management_task.h"
#include "serial.h"
#include "stratum_api.h"
#include "coinbase_decoder.h"
#include "work_queue.h"
#include "device_config.h"
#include "display.h"
#include "esp_transport.h"

#define STRATUM_USER CONFIG_STRATUM_USER
#define FALLBACK_STRATUM_USER CONFIG_FALLBACK_STRATUM_USER

#define HISTORY_LENGTH 100
#define DIFF_STRING_SIZE 10

typedef struct {
    char message[64];
    uint32_t count;
} RejectedReasonStat;

typedef struct
{
    float current_hashrate;
    float hashrate_1m;
    float hashrate_10m;
    float hashrate_1h;
    float efficiency_1m;
    float diff_1m;
    float diff_10m;
    float diff_1h;
    float efficiency_10m;
    float efficiency_1h;
    float error_percentage;
    int64_t start_time;
    uint64_t shares_accepted;
    uint64_t shares_rejected;
    uint64_t work_received;
    RejectedReasonStat rejected_reason_stats[10];
    int rejected_reason_stats_count;
    int screen_page;
    uint64_t best_nonce_diff;
    char best_diff_string[DIFF_STRING_SIZE];
    uint64_t best_session_nonce_diff;
    char best_session_diff_string[DIFF_STRING_SIZE];
    int block_found;
    bool show_new_block;
    uint64_t lifetime_block_found;  // total blocks found, persisted to NVS
    int64_t last_block_time;        // epoch seconds at find time (0 = never found)
    double last_block_diff;         // nonce diff when block was found
    double last_block_net_diff;     // network diff at find time
    bool last_block_fallback;       // true if found on fallback pool
    char last_block_url[128];       // pool URL at find time
    char * ssid;
    char wifi_status[256];
    char ip_addr_str[16]; // IP4ADDR_STRLEN_MAX
    char ipv6_addr_str[64]; // IPv6 address string with zone identifier (INET6_ADDRSTRLEN=46 + % + interface=15)
    char ap_ssid[16];
    bool ap_enabled;
    bool is_connected;
    int identify_mode_time_ms;
    char * pool_url;
    char * fallback_pool_url;
    uint16_t pool_port;
    uint16_t fallback_pool_port;
    char * pool_user;
    char * fallback_pool_user;
    char * pool_pass;
    char * fallback_pool_pass;
    uint16_t pool_difficulty;
    uint16_t fallback_pool_difficulty;
    bool pool_extranonce_subscribe;
    bool fallback_pool_extranonce_subscribe;
    uint8_t pool_coinbase_network;      // 0=disabled, 1=BTC, 2=BCH, 3=auto
    uint8_t fallback_pool_coinbase_network;
    float response_time;
    bool use_fallback_stratum;
    uint16_t pool_is_tls;
    uint16_t fallback_pool_is_tls;
    uint16_t pool_tls;
    uint16_t fallback_pool_tls;
    char * pool_cert;
    char * fallback_pool_cert;
    bool is_using_fallback;
    char pool_connection_info[64];
    bool overheat_mode;
    uint16_t power_fault;
    uint32_t lastClockSync;
    bool is_screen_active;
    bool is_firmware_update;
    char firmware_update_filename[20];
    char firmware_update_status[20];
    char * asic_status;
    char * version;
    char * axeOSVersion;
    float last_submitted_diff;
    uint32_t stratum_disconnects;
    uint32_t wifi_disconnects;
    uint32_t tx_errors;
    uint32_t rx_errors;
    float network_ping_ms;
    int64_t last_share_time;
    int64_t last_job_received_us;
    int64_t last_nonce_us;
    uint32_t asic_nonce_count;         // total nonces from all chips
    uint32_t *asic_nonce_counts;       // per-chip nonce counts (heap, size = asic_count)
    uint32_t asic_rx_failures;         // UART frames from ASIC rejected (bad length, preamble, or CRC)
    uint32_t asic_invalid_job_nonces;   // nonces returned by ASIC with an unrecognised job ID
} SystemModule;

typedef struct {
    uint32_t value;
    uint64_t time_us;
    float hashrate;
} measurement_t;

typedef struct {
    measurement_t* total_measurement;
    measurement_t** domain_measurements;
    measurement_t* error_measurement;
    float* chip_frequency;
    float* chip_temp;
    bool is_initialized;
} HashrateMonitorModule;

typedef struct
{
    bool is_active;
    bool is_finished;
    char *message;
    char *result;
    char *finished;
} SelfTestModule;

typedef struct
{
    // ASIC may not return the nonce in the same order as the jobs were sent
    // it also may return a previous nonce under some circumstances
    // so we keep a list of jobs indexed by the job id
    bm_job **active_jobs;
    // Current job to be processed (replaces ASIC_jobs_queue)
    bm_job *current_job;
    // Timestamp (esp_timer_get_time) of the last job pushed to the ASIC over UART
    int64_t last_job_sent_us;
    // Total jobs dispatched to the ASIC over UART since boot
    uint32_t asic_jobs_dispatched;
    //semaphone
    SemaphoreHandle_t semaphore;
} AsicTaskModule;

typedef struct
{
    work_queue stratum_queue;

    SystemModule SYSTEM_MODULE;
    DeviceConfig DEVICE_CONFIG;
    DisplayConfig DISPLAY_CONFIG;
    AsicTaskModule ASIC_TASK_MODULE;
    PowerManagementModule POWER_MANAGEMENT_MODULE;
    SelfTestModule SELF_TEST_MODULE;
    HashrateMonitorModule HASHRATE_MONITOR_MODULE;

    char * extranonce_str;
    int extranonce_2_len;

    uint8_t * valid_jobs;
    pthread_mutex_t valid_jobs_lock;

    uint32_t pool_difficulty;
    bool new_set_mining_difficulty_msg;
    uint32_t version_mask;
    bool new_stratum_version_rolling_msg;

    esp_transport_handle_t transport;
    
    // A message ID that must be unique per request that expects a response.
    // For requests not expecting a response (called notifications), this is null.
    int send_uid;

    bool ASIC_initalized;
    bool psram_is_available;

    int block_height;
    char scriptsig[128];
    coinbase_output_t coinbase_outputs[MAX_COINBASE_TX_OUTPUTS];
    int coinbase_output_count;
    uint64_t coinbase_value_total_satoshis;
    uint64_t coinbase_value_user_satoshis;
    uint64_t network_nonce_diff;
    char network_diff_string[DIFF_STRING_SIZE];
} GlobalState;

#endif /* GLOBAL_STATE_H_ */
