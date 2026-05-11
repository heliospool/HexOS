#include <string.h>
#include <inttypes.h>
#include <esp_heap_caps.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "system.h"
#include "asic_common.h"
#include "asic.h"
#include "utils.h"

#define EPSILON 0.0001f

#define HASHRATE_UNIT 0x100000uLL // Hashrate register unit (2^24 hashes)

#define POLL_RATE 5000
#define HASHRATE_1M_SIZE (60000 / POLL_RATE)  // 12
#define HASHRATE_10M_SIZE 10
#define HASHRATE_1H_SIZE 6
#define DIV_10M (HASHRATE_1M_SIZE)
#define DIV_1H (HASHRATE_10M_SIZE * DIV_10M)

static unsigned long poll_count = 0;
static float hashrate_1m[HASHRATE_1M_SIZE];
static float hashrate_10m_prev;
static float hashrate_10m[HASHRATE_10M_SIZE];
static float hashrate_1h_prev;
static float hashrate_1h[HASHRATE_1H_SIZE];
static float efficiency_1m[HASHRATE_1M_SIZE];
static float efficiency_10m_prev;
static float efficiency_10m[HASHRATE_10M_SIZE];
static float efficiency_1h_prev;
static float efficiency_1h[HASHRATE_1H_SIZE];

static const char *TAG = "hashrate_monitor";

static float sum_hashrates(measurement_t * measurement, int asic_count)
{
    if (asic_count == 1) return measurement[0].hashrate;

    float total = 0;
    for (int asic_nr = 0; asic_nr < asic_count; asic_nr++) {
        total += measurement[asic_nr].hashrate;
    }
    return total;
}

static void clear_measurements(GlobalState * GLOBAL_STATE)
{
    HashrateMonitorModule * HASHRATE_MONITOR_MODULE = &GLOBAL_STATE->HASHRATE_MONITOR_MODULE;

    int asic_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
    int hash_domains = GLOBAL_STATE->DEVICE_CONFIG.family.asic.hash_domains;

    memset(HASHRATE_MONITOR_MODULE->total_measurement, 0, asic_count * sizeof(measurement_t));
    memset(HASHRATE_MONITOR_MODULE->domain_measurements[0], 0, asic_count * hash_domains * sizeof(measurement_t));
    memset(HASHRATE_MONITOR_MODULE->error_measurement, 0, asic_count * sizeof(measurement_t));
}

void update_hashrate(measurement_t * measurement, uint32_t value)
{
    uint8_t flag_long = (value & 0x80000000) >> 31;
    uint32_t hashrate_value = value & 0x7FFFFFFF;    

    if (hashrate_value != 0x007FFFFF && !flag_long) {
        float hashrate = hashrate_value * (float)HASHRATE_UNIT; // Make sure it stays in float
        measurement->hashrate =  hashrate / 1e9f; // Convert to Gh/s
    }
}

void update_hash_counter(measurement_t * measurement, uint32_t value, uint64_t time_us)
{
    uint64_t previous_time_us = measurement->time_us;
    if (previous_time_us != 0) {
        uint32_t duration_us = time_us - previous_time_us;
        uint32_t counter = value - measurement->value; // Compute counter difference, handling uint32_t wraparound
        measurement->hashrate = hashCounterToGhs(duration_us, counter);
    }

    measurement->value = value;
    measurement->time_us = time_us;
}

static void init_averages()
{
    float nan_val = nanf("");
    for (int i = 0; i < HASHRATE_1M_SIZE; i++) hashrate_1m[i] = nan_val;
    for (int i = 0; i < HASHRATE_10M_SIZE; i++) hashrate_10m[i] = nan_val;
    for (int i = 0; i < HASHRATE_1H_SIZE; i++) hashrate_1h[i] = nan_val;
    for (int i = 0; i < HASHRATE_1M_SIZE; i++) efficiency_1m[i] = nan_val;
    for (int i = 0; i < HASHRATE_10M_SIZE; i++) efficiency_10m[i] = nan_val;
    for (int i = 0; i < HASHRATE_1H_SIZE; i++) efficiency_1h[i] = nan_val;
}

static float calculate_avg_nan_safe(const float arr[], int size) {
    float sum = 0.0f;
    int count = 0;
    for (int i = 0; i < size; i++) {
        if (!isnanf(arr[i])) {
            sum += arr[i];
            count++;
        }
    }
    return (count > 0) ? (sum / count) : 0.0f;
}

static void update_hashrate_averages(SystemModule * SYSTEM_MODULE)
{
    hashrate_1m[poll_count % HASHRATE_1M_SIZE] = SYSTEM_MODULE->current_hashrate;
    SYSTEM_MODULE->hashrate_1m = calculate_avg_nan_safe(hashrate_1m, HASHRATE_1M_SIZE);

    int hashrate_10m_blend = poll_count % HASHRATE_1M_SIZE;
    if (hashrate_10m_blend == 0) {
        hashrate_10m_prev = hashrate_10m[(poll_count / DIV_10M) % HASHRATE_10M_SIZE];
    }
    float hashrate_1m_value = SYSTEM_MODULE->hashrate_1m;
    if (!isnanf(hashrate_10m_prev)) {
        float f = (hashrate_10m_blend + 1.0f) / (float)HASHRATE_1M_SIZE;
        hashrate_1m_value = f * hashrate_1m_value + (1.0f - f) * hashrate_10m_prev;
    }

    hashrate_10m[(poll_count / DIV_10M) % HASHRATE_10M_SIZE] = hashrate_1m_value;
    SYSTEM_MODULE->hashrate_10m = calculate_avg_nan_safe(hashrate_10m, HASHRATE_10M_SIZE);

    int hashrate_1h_blend = poll_count % DIV_1H;
    if (hashrate_1h_blend == 0) {
        hashrate_1h_prev = hashrate_1h[(poll_count / DIV_1H) % HASHRATE_1H_SIZE];
    }
    float hashrate_10m_value = SYSTEM_MODULE->hashrate_10m;
    if (!isnanf(hashrate_1h_prev)) {
        float f = (hashrate_1h_blend + 1.0f) / (float)DIV_1H;
        hashrate_10m_value = f * hashrate_10m_value + (1.0f - f) * hashrate_1h_prev;
    }

    hashrate_1h[(poll_count / DIV_1H) % HASHRATE_1H_SIZE] = hashrate_10m_value;
    SYSTEM_MODULE->hashrate_1h = calculate_avg_nan_safe(hashrate_1h, HASHRATE_1H_SIZE);

    poll_count++;
}

static void update_efficiency_averages(SystemModule * SYSTEM_MODULE, float power)
{
    // Efficiency in J/Th: power (W) / hashrate (Th/s) = power / (hashrate_Ghs / 1000)
    float current_eff = (SYSTEM_MODULE->current_hashrate > 0.0f)
        ? power / (SYSTEM_MODULE->current_hashrate / 1000.0f)
        : nanf("");

    efficiency_1m[poll_count % HASHRATE_1M_SIZE] = current_eff;
    SYSTEM_MODULE->efficiency_1m = calculate_avg_nan_safe(efficiency_1m, HASHRATE_1M_SIZE);

    int eff_10m_blend = poll_count % HASHRATE_1M_SIZE;
    if (eff_10m_blend == 0) {
        efficiency_10m_prev = efficiency_10m[(poll_count / DIV_10M) % HASHRATE_10M_SIZE];
    }
    float eff_1m_value = SYSTEM_MODULE->efficiency_1m;
    if (!isnanf(efficiency_10m_prev)) {
        float f = (eff_10m_blend + 1.0f) / (float)HASHRATE_1M_SIZE;
        eff_1m_value = f * eff_1m_value + (1.0f - f) * efficiency_10m_prev;
    }

    efficiency_10m[(poll_count / DIV_10M) % HASHRATE_10M_SIZE] = eff_1m_value;
    SYSTEM_MODULE->efficiency_10m = calculate_avg_nan_safe(efficiency_10m, HASHRATE_10M_SIZE);

    int eff_1h_blend = poll_count % DIV_1H;
    if (eff_1h_blend == 0) {
        efficiency_1h_prev = efficiency_1h[(poll_count / DIV_1H) % HASHRATE_1H_SIZE];
    }
    float eff_10m_value = SYSTEM_MODULE->efficiency_10m;
    if (!isnanf(efficiency_1h_prev)) {
        float f = (eff_1h_blend + 1.0f) / (float)DIV_1H;
        eff_10m_value = f * eff_10m_value + (1.0f - f) * efficiency_1h_prev;
    }

    efficiency_1h[(poll_count / DIV_1H) % HASHRATE_1H_SIZE] = eff_10m_value;
    SYSTEM_MODULE->efficiency_1h = calculate_avg_nan_safe(efficiency_1h, HASHRATE_1H_SIZE);
}

void HASHRATE_update_diff_averages(SystemModule *sys_module, float diff)
{
    int64_t now_us = esp_timer_get_time();
    static int64_t last_us = 0;

    if (last_us == 0) {
        // First share — initialise with this diff
        sys_module->diff_1m  = diff;
        sys_module->diff_10m = diff;
        sys_module->diff_1h  = diff;
        last_us = now_us;
        return;
    }

    float dt = (now_us - last_us) / 1e6f; // seconds since last share
    last_us = now_us;

    float alpha_1m  = 1.0f - expf(-dt / 60.0f);
    float alpha_10m = 1.0f - expf(-dt / 600.0f);
    float alpha_1h  = 1.0f - expf(-dt / 3600.0f);

    sys_module->diff_1m  = alpha_1m  * diff + (1.0f - alpha_1m)  * sys_module->diff_1m;
    sys_module->diff_10m = alpha_10m * diff + (1.0f - alpha_10m) * sys_module->diff_10m;
    sys_module->diff_1h  = alpha_1h  * diff + (1.0f - alpha_1h)  * sys_module->diff_1h;
}

void hashrate_monitor_task(void *pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *)pvParameters;
    HashrateMonitorModule * HASHRATE_MONITOR_MODULE = &GLOBAL_STATE->HASHRATE_MONITOR_MODULE;
    SystemModule * SYSTEM_MODULE = &GLOBAL_STATE->SYSTEM_MODULE;

    int asic_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
    int hash_domains = GLOBAL_STATE->DEVICE_CONFIG.family.asic.hash_domains;

    HASHRATE_MONITOR_MODULE->total_measurement = heap_caps_malloc(asic_count * sizeof(measurement_t), MALLOC_CAP_SPIRAM);
    measurement_t* data = heap_caps_malloc(asic_count * hash_domains * sizeof(measurement_t), MALLOC_CAP_SPIRAM);
    HASHRATE_MONITOR_MODULE->domain_measurements = heap_caps_malloc(asic_count * sizeof(measurement_t*), MALLOC_CAP_SPIRAM);
    for (size_t asic_nr = 0; asic_nr < asic_count; asic_nr++) {
        HASHRATE_MONITOR_MODULE->domain_measurements[asic_nr] = data + (asic_nr * hash_domains);
    }
    HASHRATE_MONITOR_MODULE->error_measurement = heap_caps_malloc(asic_count * sizeof(measurement_t), MALLOC_CAP_SPIRAM);
    HASHRATE_MONITOR_MODULE->chip_frequency = heap_caps_calloc(asic_count, sizeof(float), MALLOC_CAP_SPIRAM);
    HASHRATE_MONITOR_MODULE->chip_temp = heap_caps_calloc(asic_count, sizeof(float), MALLOC_CAP_SPIRAM);

    clear_measurements(GLOBAL_STATE);

    init_averages();

    HASHRATE_MONITOR_MODULE->is_initialized = true;

    TickType_t taskWakeTime = xTaskGetTickCount();
    while (1) {
        ASIC_read_registers(GLOBAL_STATE);

        vTaskDelay(100 / portTICK_PERIOD_MS);

        float current_hashrate = sum_hashrates(HASHRATE_MONITOR_MODULE->total_measurement, asic_count);
        float error_hashrate = sum_hashrates(HASHRATE_MONITOR_MODULE->error_measurement, asic_count);

        SYSTEM_MODULE->current_hashrate = current_hashrate;
        SYSTEM_MODULE->error_percentage = current_hashrate > 0 ? error_hashrate / current_hashrate * 100.f : 0;

        if(current_hashrate > 0.0f) {
            update_hashrate_averages(SYSTEM_MODULE);
            update_efficiency_averages(SYSTEM_MODULE, GLOBAL_STATE->POWER_MANAGEMENT_MODULE.power);
        }

        vTaskDelayUntil(&taskWakeTime, POLL_RATE / portTICK_PERIOD_MS);
    }
}

void hashrate_monitor_register_read(void *pvParameters, register_type_t register_type, uint8_t asic_nr, uint32_t value)
{
    uint64_t time_us = esp_timer_get_time();

    GlobalState * GLOBAL_STATE = (GlobalState *)pvParameters;
    HashrateMonitorModule * HASHRATE_MONITOR_MODULE = &GLOBAL_STATE->HASHRATE_MONITOR_MODULE;

    int asic_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;

    if (asic_nr >= asic_count) {
        ESP_LOGE(TAG, "Asic nr out of bounds [%d]", asic_nr);
        return;
    }

    switch(register_type) {
        case REGISTER_HASHRATE:
            update_hashrate(&HASHRATE_MONITOR_MODULE->total_measurement[asic_nr], value);
            update_hashrate(&HASHRATE_MONITOR_MODULE->domain_measurements[asic_nr][0], value);
            break;
        case REGISTER_TOTAL_COUNT:
            update_hash_counter(&HASHRATE_MONITOR_MODULE->total_measurement[asic_nr], value, time_us);
            break;
        case REGISTER_DOMAIN_0_COUNT:
            update_hash_counter(&HASHRATE_MONITOR_MODULE->domain_measurements[asic_nr][0], value, time_us);
            break;
        case REGISTER_DOMAIN_1_COUNT:
            update_hash_counter(&HASHRATE_MONITOR_MODULE->domain_measurements[asic_nr][1], value, time_us);
            break;
        case REGISTER_DOMAIN_2_COUNT:
            update_hash_counter(&HASHRATE_MONITOR_MODULE->domain_measurements[asic_nr][2], value, time_us);
            break;
        case REGISTER_DOMAIN_3_COUNT:
            update_hash_counter(&HASHRATE_MONITOR_MODULE->domain_measurements[asic_nr][3], value, time_us);
            break;
        case REGISTER_ERROR_COUNT:
            update_hash_counter(&HASHRATE_MONITOR_MODULE->error_measurement[asic_nr], value, time_us);
            break;
        case REGISTER_PLL_PARAM: {
            // Decode chip frequency from PLL divider bytes.
            // UART packet bytes 2-5: [vdo_scale, fb_divider, refdiv, postdiv].
            // After ntohl (bswap32 on little-endian ESP32):
            //   bits 31:24 = vdo_scale, bits 23:16 = fb_divider, bits 15:8 = refdiv, bits 7:0 = postdiv
            uint8_t fb_divider = (value >> 16) & 0xFF;
            uint8_t refdiv     = (value >> 8) & 0xFF;
            uint8_t postdiv    = (value >> 0) & 0xFF;
            uint8_t postdiv1   = ((postdiv >> 4) & 0xF) + 1;
            uint8_t postdiv2   = (postdiv & 0xF) + 1;
            if (fb_divider > 0 && refdiv > 0 && postdiv1 > 0 && postdiv2 > 0) {
                HASHRATE_MONITOR_MODULE->chip_frequency[asic_nr] =
                    25.0f * fb_divider / refdiv / postdiv1 / postdiv2;
                ESP_LOGD(TAG, "asic %d pll: fb=%u refdiv=%u pd1=%u pd2=%u -> %.2f MHz",
                         asic_nr, fb_divider, refdiv, postdiv1, postdiv2,
                         HASHRATE_MONITOR_MODULE->chip_frequency[asic_nr]);
            }
            break;
        }
        case REGISTER_INVALID:
            ESP_LOGE(TAG, "Invalid register type");
            break;
        case REGISTER_TEMP: {
            float ftemp = (float)(value & 0x0000ffff) * 0.171342f - 299.5144f;
            if (ftemp > -40.0f && ftemp < 150.0f) {
                HASHRATE_MONITOR_MODULE->chip_temp[asic_nr] = ftemp;
                ESP_LOGI(TAG, "asic %d on-die temp: %.1f°C", asic_nr, ftemp);
            }
            break;
        }
    }
}

