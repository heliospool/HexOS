#include <string.h>
#include "INA260.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "fan_controller_task.h"
#include "math.h"
#include "mining.h"
#include "nvs_config.h"
#include "serial.h"
#include "TPS546.h"
#include "vcore.h"
#include "thermal.h"
#include "PID.h"
#include "power.h"
#include "asic.h"

#define EPSILON 0.0001f
#define POLL_TIME_MS 100
#define LOG_TIME_MS 2000

static const char * TAG = "fan_controller";

void FAN_CONTROLLER_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    float pid_p = nvs_config_get_float(NVS_CONFIG_PID_P);
    float pid_i = nvs_config_get_float(NVS_CONFIG_PID_I);
    float pid_d = nvs_config_get_float(NVS_CONFIG_PID_D);
    // Asymmetric slew rate: spin up instantly, slow down at max 1% per 100ms cycle
    // Prevents oscillation while still reacting aggressively to rising temps
    float fan_decrease_rate = nvs_config_get_float(NVS_CONFIG_FAN_DECREASE_RATE);
    ESP_LOGI(TAG, "PID: P=%.4f I=%.4f D=%.4f  fan_dec_rate=%.4f", pid_p, pid_i, pid_d, fan_decrease_rate);

    PIDController pid = {0};

    float pid_input = 0;
    float pid_output = 0;
    float pid_setPoint = nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET);
    uint16_t pid_output_min = 0;
    int log_counter = 0;

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    // Initialize PID controller with pid_d_startup and PID_REVERSE directly
    pid_init(&pid, &pid_input, &pid_output, &pid_setPoint, pid_p, pid_i, pid_d, PID_P_ON_E, PID_REVERSE);
    pid_set_sample_time(&pid, POLL_TIME_MS); // Sample time in ms
    pid_set_mode(&pid, AUTOMATIC);        // This calls pid_initialize() internally

    ESP_LOGI(TAG, "P:%.1f I:%.1f D:%.1f", pid.dispKp, pid.dispKi, pid.dispKd);

    TickType_t taskWakeTime = xTaskGetTickCount();

    while (1) {
        if (nvs_config_get_bool(NVS_CONFIG_OVERHEAT_MODE)) {
            if (fabs(power_management->fan_perc - 100) > EPSILON) {
                ESP_LOGW(TAG, "Overheat mode, setting fan to 100%%");
                power_management->fan_perc = 100;
                if (Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, 1.0f) != ESP_OK) {
                    exit(EXIT_FAILURE);
                }
            }
        } else {
            //enable the PID auto control for the FAN if set
            if (nvs_config_get_bool(NVS_CONFIG_AUTO_FAN_SPEED)) {

                // Refresh PID setpoint from NVS in case it was changed via API
                pid_setPoint = nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET);

                uint16_t new_pid_output_min = nvs_config_get_u16(NVS_CONFIG_MIN_FAN_SPEED);
                if (pid_output_min != new_pid_output_min) {
                    pid_output_min = new_pid_output_min;
                    pid_set_output_limits(&pid, pid_output_min, 100);
                }

                // Use the highest valid ASIC temperature; fall back to whichever sensor is working
                float t1 = power_management->chip_temp_avg;
                float t2 = power_management->chip_temp2_avg;
                float best_temp = -1;
                if (t1 >= 0 && t2 >= 0) {
                    best_temp = t1 > t2 ? t1 : t2;
                } else if (t1 >= 0) {
                    best_temp = t1;
                } else if (t2 >= 0) {
                    best_temp = t2;
                }

                if (best_temp >= 0) {
                    pid_input = best_temp;
                    pid_compute(&pid);
                    // Clamp PID output to valid range to prevent overshoot above 100%
                    if (pid_output > 100) pid_output = 100;
                    if (pid_output < 0) pid_output = 0;
                    // Uncomment for debugging PID output directly after compute
                    // ESP_LOGD(TAG, "DEBUG: PID raw output: %.2f%%, Input: %.1f, SetPoint: %.1f", pid_output, pid_input, pid_setPoint);

                    if (fabs(power_management->fan_perc - pid_output) > EPSILON) {
                        float new_perc;
                        if (pid_output > power_management->fan_perc) {
                            // Temp rising or still above target — spin up immediately
                            new_perc = pid_output;
                        } else if (pid_input > pid_setPoint) {
                            // Still above target — hold current fan speed, don't reduce yet
                            new_perc = power_management->fan_perc;
                        } else if (power_management->fan_perc - pid_output > fan_decrease_rate) {
                            // Below target — slow down gradually
                            new_perc = power_management->fan_perc - fan_decrease_rate;
                        } else {
                            new_perc = pid_output;
                        }
                        power_management->fan_perc = new_perc;
                        if (Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, new_perc / 100.0f) != ESP_OK) {
                            exit(EXIT_FAILURE);
                        }
                    }

                    log_counter += POLL_TIME_MS;
                    if (log_counter >= LOG_TIME_MS) {
                        log_counter -= LOG_TIME_MS;
                        ESP_LOGI(TAG, "Temp: %.1f °C, SetPoint: %.1f °C, Output: %.1f%%", pid_input, pid_setPoint, pid_output);
                    }
                } else {
                    // Both temperature sensors invalid — run fan at 100% to prevent thermal damage
                    if (fabs(power_management->fan_perc - 100) > EPSILON) {
                        ESP_LOGW(TAG, "All temperature sensors invalid, setting fan to 100%%");
                        power_management->fan_perc = 100;
                        if (Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, 1.0f) != ESP_OK) {
                            exit(EXIT_FAILURE);
                        }
                    }
                }
            } else { // Manual fan speed
                uint16_t fan_perc = nvs_config_get_u16(NVS_CONFIG_MANUAL_FAN_SPEED);
                if (fan_perc > 100) fan_perc = 100;
                if (fabs(power_management->fan_perc - fan_perc) > EPSILON) {
                    power_management->fan_perc = fan_perc;
                    if (Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, fan_perc / 100.0f) != ESP_OK) {
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }

        power_management->fan_rpm = Thermal_get_fan_speed(&GLOBAL_STATE->DEVICE_CONFIG);
        power_management->fan2_rpm = Thermal_get_fan2_speed(&GLOBAL_STATE->DEVICE_CONFIG);

        vTaskDelayUntil(&taskWakeTime, POLL_TIME_MS / portTICK_PERIOD_MS);
    }
}
